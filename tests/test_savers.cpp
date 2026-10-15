// Locks in the streaming savers: exact PPM bytes, lossless PNG round trip,
// JPEG dimensions/quality, cancel and error handling, non-ASCII paths.
#include <doctest/doctest.h>
#include "savers.h"
#include "render_settings.h"
#include "test_util.h"

#include <png.h>
extern "C" {
#include <jpeglib.h>
}
#include <cstdlib>
#include <vector>

namespace fs = std::filesystem;
using testutil::TempDir;

namespace {

constexpr uint32_t kW = 37, kH = 19;

uint8_t patR(uint32_t x, uint32_t)   { return (uint8_t)(x * 7); }
uint8_t patG(uint32_t, uint32_t y)   { return (uint8_t)(y * 13); }
uint8_t patB(uint32_t x, uint32_t y) { return (uint8_t)((x + y) * 3); }

// Fills an MmapImage with a known RGBA pattern (alpha deliberately != 255 so
// we notice if a saver ever leaks it).
void fillPattern(MmapImage& img) {
    for (uint32_t y = 0; y < img.height(); ++y) {
        uint8_t* row = img.rowPtr(y);
        for (uint32_t x = 0; x < img.width(); ++x) {
            row[x * 4 + 0] = patR(x, y);
            row[x * 4 + 1] = patG(x, y);
            row[x * 4 + 2] = patB(x, y);
            row[x * 4 + 3] = 0x5A;
        }
    }
}

std::vector<uint8_t> expectedRGB(uint32_t w, uint32_t h) {
    std::vector<uint8_t> v;
    v.reserve((size_t)w * h * 3);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            v.push_back(patR(x, y));
            v.push_back(patG(x, y));
            v.push_back(patB(x, y));
        }
    return v;
}

std::vector<uint8_t> readAll(const fs::path& p) {
    std::vector<uint8_t> data;
    FILE* f = testutil::openFile(p, "rb");
    if (!f) return data;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    return data;
}

bool decodePNG(const fs::path& p, uint32_t& w, uint32_t& h, std::vector<uint8_t>& rgb) {
    FILE* f = testutil::openFile(p, "rb");
    if (!f) return false;
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    bool ok = png_image_begin_read_from_stdio(&image, f) != 0;
    if (ok) {
        image.format = PNG_FORMAT_RGB;
        rgb.assign(PNG_IMAGE_SIZE(image), 0);
        ok = png_image_finish_read(&image, nullptr, rgb.data(), 0, nullptr) != 0;
        w = image.width;
        h = image.height;
    }
    png_image_free(&image);
    std::fclose(f);
    return ok;
}

bool decodeJPEG(const fs::path& p, uint32_t& w, uint32_t& h, int& comps, std::vector<uint8_t>& rgb) {
    FILE* f = testutil::openFile(p, "rb");
    if (!f) return false;
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);
    w = cinfo.output_width;
    h = cinfo.output_height;
    comps = cinfo.output_components;
    rgb.assign((size_t)w * h * comps, 0);
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = rgb.data() + (size_t)cinfo.output_scanline * w * comps;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::fclose(f);
    return true;
}

struct PatternImage {
    TempDir scratch{ "savers_scratch" };
    MmapImage img;
    PatternImage(uint32_t w = kW, uint32_t h = kH) {
        REQUIRE(img.create(scratch.path().wstring(), w, h));
        fillPattern(img);
    }
};

} // namespace

TEST_CASE("PPM output is byte-exact P6") {
    PatternImage src;
    TempDir out("savers");
    fs::path p = out.path() / "out.ppm";
    SaveProgress sp;
    REQUIRE(savePPM(src.img, p.wstring(), sp));
    CHECK(sp.progress.load() == 1.0f);

    std::string header = "P6\n37 19\n255\n";
    std::vector<uint8_t> expected(header.begin(), header.end());
    auto rgb = expectedRGB(kW, kH);
    expected.insert(expected.end(), rgb.begin(), rgb.end());
    CHECK(readAll(p) == expected);
}

TEST_CASE("PNG round-trips losslessly") {
    PatternImage src;
    TempDir out("savers");
    fs::path p = out.path() / "out.png";
    SaveProgress sp;
    REQUIRE(savePNG(src.img, p.wstring(), sp));
    CHECK(sp.progress.load() == 1.0f);

    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgb;
    REQUIRE(decodePNG(p, w, h, rgb));
    CHECK(w == kW);
    CHECK(h == kH);
    CHECK(rgb == expectedRGB(kW, kH));
}

TEST_CASE("JPEG has the right size and stays close to the source") {
    PatternImage src;
    TempDir out("savers");
    fs::path p = out.path() / "out.jpg";
    SaveProgress sp;
    REQUIRE(saveJPEG(src.img, p.wstring(), 95, sp));
    CHECK(sp.progress.load() == 1.0f);

    uint32_t w = 0, h = 0;
    int comps = 0;
    std::vector<uint8_t> rgb;
    REQUIRE(decodeJPEG(p, w, h, comps, rgb));
    CHECK(w == kW);
    CHECK(h == kH);
    CHECK(comps == 3);

    auto ref = expectedRGB(kW, kH);
    REQUIRE(rgb.size() == ref.size());
    double sum = 0;
    for (size_t i = 0; i < ref.size(); ++i) sum += std::abs((int)rgb[i] - (int)ref[i]);
    double mae = sum / ref.size();
    CAPTURE(mae);
    CHECK(mae < 10.0);
}

TEST_CASE("JPEG refuses images wider than 65500 px and writes nothing") {
    PatternImage src(kJpegMaxSide + 1, 1);
    TempDir out("savers");
    fs::path p = out.path() / "wide.jpg";
    SaveProgress sp;
    CHECK_FALSE(saveJPEG(src.img, p.wstring(), 90, sp));
    CHECK_FALSE(sp.message.empty());
    CHECK_FALSE(fs::exists(p));
}

TEST_CASE("cancel before the first row returns false and deletes the partial file") {
    PatternImage src;
    TempDir out("savers");
    const SaveFormat formats[] = { SaveFormat::PNG, SaveFormat::JPEG, SaveFormat::PPM };
    const char* names[] = { "c.png", "c.jpg", "c.ppm" };
    for (int i = 0; i < 3; ++i) {
        CAPTURE(names[i]);
        fs::path p = out.path() / names[i];
        SaveProgress sp;
        sp.cancel.store(true);
        CHECK_FALSE(saveImage(src.img, p.wstring(), formats[i], 90, sp));
        CHECK(sp.message == "Save cancelled.");
        CHECK(sp.done.load());
        CHECK_FALSE(sp.success.load());
        CHECK_FALSE(fs::exists(p));
    }
}

TEST_CASE("unwritable destination fails with a message") {
    PatternImage src;
    TempDir out("savers");
    const SaveFormat formats[] = { SaveFormat::PNG, SaveFormat::JPEG, SaveFormat::PPM };
    for (SaveFormat f : formats) {
        fs::path p = out.path() / "no_such_dir" / "x.out";
        SaveProgress sp;
        CHECK_FALSE(saveImage(src.img, p.wstring(), f, 90, sp));
        CHECK_FALSE(sp.message.empty());
        CHECK(sp.done.load());
        CHECK_FALSE(sp.success.load());
    }
}

TEST_CASE("saveImage reports success and non-ASCII file names work") {
    PatternImage src;
    TempDir out("savers");
    fs::path p = out.path() / fs::path(u8"tëst_日本.ppm");
    SaveProgress sp;
    CHECK(saveImage(src.img, p.wstring(), SaveFormat::PPM, 90, sp));
    CHECK(sp.done.load());
    CHECK(sp.success.load());
    CHECK(fs::exists(p));
    CHECK(fs::file_size(p) == std::string("P6\n37 19\n255\n").size() + (size_t)kW * kH * 3);
}
