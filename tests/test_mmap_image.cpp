// Locks in MmapImage: geometry, read/write through the mapping, scratch-file
// naming and lifetime, and failure handling.
#include <doctest/doctest.h>
#include "mmap_image.h"
#include "test_util.h"

namespace fs = std::filesystem;
using testutil::TempDir;

TEST_CASE("requiredBytes handles gigapixel sizes without overflow") {
    CHECK(MmapImage::requiredBytes(76800, 43200) == 13271040000ull);
    CHECK(MmapImage::requiredBytes(1920, 1080) == 8294400ull);
}

TEST_CASE("create maps a writable RGBA8 image of the right size") {
    TempDir dir("mmap");
    MmapImage img;
    REQUIRE(img.create(dir.path().wstring(), 37, 19));
    CHECK(img.valid());
    CHECK(img.width() == 37);
    CHECK(img.height() == 19);
    CHECK(img.sizeBytes() == 37ull * 19 * 4);
    CHECK(img.rowPtr(1) - img.rowPtr(0) == 37 * 4);
    CHECK(img.rowPtr(0) == img.data());

    for (uint64_t i = 0; i < img.sizeBytes(); ++i) img.data()[i] = (uint8_t)(i * 31 + 7);
    bool same = true;
    for (uint64_t i = 0; i < img.sizeBytes(); ++i)
        if (img.data()[i] != (uint8_t)(i * 31 + 7)) { same = false; break; }
    CHECK(same);
}

TEST_CASE("scratch file lives in the folder while open and is deleted on destroy") {
    TempDir dir("mmap");
    MmapImage img;
    REQUIRE(img.create(dir.path().wstring(), 16, 8));

    fs::path p = img.path();
    CHECK(p.parent_path() == dir.path());
    std::string name = p.filename().string();
    CHECK(name.rfind("VkGigaTracer_", 0) == 0);
    CHECK(p.extension() == ".scratch");
    CHECK(fs::exists(p));

    img.destroy();
    CHECK_FALSE(img.valid());
    CHECK(img.width() == 0);
    CHECK(img.path().empty());
    CHECK_FALSE(fs::exists(p));
}

TEST_CASE("destructor also deletes the scratch file") {
    TempDir dir("mmap");
    fs::path p;
    {
        MmapImage img;
        REQUIRE(img.create(dir.path().wstring(), 4, 4));
        p = img.path();
        CHECK(fs::exists(p));
    }
    CHECK_FALSE(fs::exists(p));
}

TEST_CASE("re-creating replaces the previous scratch file") {
    TempDir dir("mmap");
    MmapImage img;
    REQUIRE(img.create(dir.path().wstring(), 4, 4));
    fs::path first = img.path();
    REQUIRE(img.create(dir.path().wstring(), 8, 2));
    CHECK(img.path() != first.wstring());
    CHECK_FALSE(fs::exists(first));
    CHECK(img.width() == 8);
    CHECK(img.height() == 2);
}

TEST_CASE("create fails cleanly in a folder that does not exist") {
    TempDir dir("mmap");
    MmapImage img;
    CHECK_FALSE(img.create((dir.path() / "does_not_exist").wstring(), 4, 4));
    CHECK_FALSE(img.valid());
    CHECK(img.width() == 0);
    CHECK(img.height() == 0);
}

TEST_CASE("resolveFolder and freeSpace") {
    TempDir dir("mmap");
    CHECK(fs::path(MmapImage::resolveFolder(dir.path().wstring())) == dir.path());

    fs::path def = MmapImage::resolveFolder(L"");
    CHECK_FALSE(def.empty());
    CHECK(fs::is_directory(def));

    uint64_t freeBytes = 0;
    CHECK(MmapImage::freeSpace(dir.path().wstring(), freeBytes));
    CHECK(freeBytes > 0);
}
