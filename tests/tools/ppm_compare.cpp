// ppm_compare - golden-image check for the --selftest render.
//
//   ppm_compare <render.ppm> <reference.png> [--min-block-psnr dB] [--max-mae v]
//       Compares a P6 PPM against a PNG reference. Exit 0 = pass.
//   ppm_compare --make-ref <render.ppm> <reference.png>
//       Writes the PPM out as the PNG reference.
//
// Path tracing at 16 spp is noisy, and different GPUs/drivers diverge per
// pixel even with identical seeds, so the main criterion is PSNR over 16x16
// block averages (noise cancels, structure/colour/brightness do not). The
// full-resolution mean absolute error is a looser second guard.
#include <png.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Image {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> rgb;
};

bool readPPM(const char* path, Image& img) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return false; }
    char magic[3] = {};
    unsigned w = 0, h = 0, maxv = 0;
    bool ok = std::fscanf(f, "%2s %u %u %u", magic, &w, &h, &maxv) == 4 &&
              std::strcmp(magic, "P6") == 0 && maxv == 255;
    if (ok) {
        std::fgetc(f); // single whitespace after maxval
        img.w = w;
        img.h = h;
        img.rgb.resize((size_t)w * h * 3);
        ok = std::fread(img.rgb.data(), 1, img.rgb.size(), f) == img.rgb.size();
    }
    std::fclose(f);
    if (!ok) std::fprintf(stderr, "%s is not a valid 8-bit P6 PPM\n", path);
    return ok;
}

bool readPNG(const char* path, Image& img) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); return false; }
    png_image pi{};
    pi.version = PNG_IMAGE_VERSION;
    bool ok = png_image_begin_read_from_stdio(&pi, f) != 0;
    if (ok) {
        pi.format = PNG_FORMAT_RGB;
        img.w = pi.width;
        img.h = pi.height;
        img.rgb.resize(PNG_IMAGE_SIZE(pi));
        ok = png_image_finish_read(&pi, nullptr, img.rgb.data(), 0, nullptr) != 0;
    }
    png_image_free(&pi);
    std::fclose(f);
    if (!ok) std::fprintf(stderr, "cannot decode %s\n", path);
    return ok;
}

bool writePNG(const char* path, const Image& img) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::fprintf(stderr, "cannot create %s\n", path); return false; }
    png_image pi{};
    pi.version = PNG_IMAGE_VERSION;
    pi.width = img.w;
    pi.height = img.h;
    pi.format = PNG_FORMAT_RGB;
    bool ok = png_image_write_to_stdio(&pi, f, 0, img.rgb.data(), 0, nullptr) != 0;
    png_image_free(&pi);
    std::fclose(f);
    return ok;
}

double psnr(double mse) { return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse); }

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--make-ref") == 0) {
        Image img;
        if (!readPPM(argv[2], img) || !writePNG(argv[3], img)) return 2;
        std::printf("wrote reference %s (%ux%u)\n", argv[3], img.w, img.h);
        return 0;
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: ppm_compare <render.ppm> <reference.png> "
                             "[--min-block-psnr dB] [--max-mae v]\n"
                             "       ppm_compare --make-ref <render.ppm> <reference.png>\n");
        return 2;
    }
    double minBlockPsnr = 35.0, maxMae = 6.0;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (std::strcmp(argv[i], "--min-block-psnr") == 0) minBlockPsnr = std::atof(argv[i + 1]);
        else if (std::strcmp(argv[i], "--max-mae") == 0) maxMae = std::atof(argv[i + 1]);
    }

    Image a, b;
    if (!readPPM(argv[1], a) || !readPNG(argv[2], b)) return 2;
    if (a.w != b.w || a.h != b.h) {
        std::fprintf(stderr, "FAIL: size %ux%u vs reference %ux%u\n", a.w, a.h, b.w, b.h);
        return 1;
    }

    double absSum = 0, sqSum = 0;
    for (size_t i = 0; i < a.rgb.size(); ++i) {
        double d = (double)a.rgb[i] - (double)b.rgb[i];
        absSum += std::fabs(d);
        sqSum += d * d;
    }
    double mae = absSum / a.rgb.size();
    double fullPsnr = psnr(sqSum / a.rgb.size());

    const uint32_t B = 16;
    double blockSq = 0;
    size_t blockN = 0;
    for (uint32_t by = 0; by + B <= a.h; by += B)
        for (uint32_t bx = 0; bx + B <= a.w; bx += B)
            for (int c = 0; c < 3; ++c) {
                double sa = 0, sb = 0;
                for (uint32_t y = by; y < by + B; ++y)
                    for (uint32_t x = bx; x < bx + B; ++x) {
                        size_t i = ((size_t)y * a.w + x) * 3 + c;
                        sa += a.rgb[i];
                        sb += b.rgb[i];
                    }
                double d = (sa - sb) / (B * B);
                blockSq += d * d;
                ++blockN;
            }
    double blockPsnr = psnr(blockSq / blockN);

    bool pass = blockPsnr >= minBlockPsnr && mae <= maxMae;
    std::printf("%s: %ux%u  MAE=%.3f (max %.2f)  PSNR=%.2f dB  block16 PSNR=%.2f dB (min %.1f)\n",
                pass ? "PASS" : "FAIL", a.w, a.h, mae, maxMae, fullPsnr, blockPsnr, minBlockPsnr);
    return pass ? 0 : 1;
}
