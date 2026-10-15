// Locks in MmapImage: geometry, read/write through the mapping, scratch-file
// naming and lifetime, and failure handling.
#include <doctest/doctest.h>
#include "mmap_image.h"
#include "test_util.h"

namespace fs = std::filesystem;
using testutil::TempDir;

// Windows keeps the scratch file visible until it is closed (delete-on-close);
// Linux unlinks it right after creating it so a crash can never leak it.
#ifdef _WIN32
constexpr bool kScratchVisibleWhileOpen = true;
#else
constexpr bool kScratchVisibleWhileOpen = false;
#endif

TEST_CASE("requiredBytes handles gigapixel sizes without overflow") {
    CHECK(MmapImage::requiredBytes(76800, 43200) == 13271040000ull);
    CHECK(MmapImage::requiredBytes(1920, 1080) == 8294400ull);
}

TEST_CASE("create maps a writable RGBA8 image of the right size") {
    TempDir dir("mmap");
    MmapImage img;
    REQUIRE(img.create(dir.path(), 37, 19));
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
    REQUIRE(img.create(dir.path(), 16, 8));

    fs::path p = img.path();
    CHECK(p.parent_path() == dir.path());
    std::string name = p.filename().string();
    CHECK(name.rfind("VkGigaTracer_", 0) == 0);
    CHECK(p.extension() == ".scratch");
    CHECK(fs::exists(p) == kScratchVisibleWhileOpen);

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
        REQUIRE(img.create(dir.path(), 4, 4));
        p = img.path();
        CHECK(fs::exists(p) == kScratchVisibleWhileOpen);
    }
    CHECK_FALSE(fs::exists(p));
}

TEST_CASE("re-creating replaces the previous scratch file") {
    TempDir dir("mmap");
    MmapImage img;
    REQUIRE(img.create(dir.path(), 4, 4));
    fs::path first = img.path();
    REQUIRE(img.create(dir.path(), 8, 2));
    CHECK(img.path() != first);
    CHECK_FALSE(fs::exists(first));
    CHECK(img.width() == 8);
    CHECK(img.height() == 2);
}

TEST_CASE("create fails cleanly in a folder that does not exist") {
    TempDir dir("mmap");
    MmapImage img;
    CHECK_FALSE(img.create((dir.path() / "does_not_exist"), 4, 4));
    CHECK_FALSE(img.valid());
    CHECK(img.width() == 0);
    CHECK(img.height() == 0);
}

TEST_CASE("resolveFolder and freeSpace") {
    TempDir dir("mmap");
    CHECK(fs::path(MmapImage::resolveFolder(dir.path())) == dir.path());

    fs::path def = MmapImage::resolveFolder({});
    CHECK_FALSE(def.empty());
    CHECK(fs::is_directory(def));

    uint64_t freeBytes = 0;
    CHECK(MmapImage::freeSpace(dir.path(), freeBytes));
    CHECK(freeBytes > 0);
}

TEST_CASE("default scratch folder is <exe dir>/temp") {
    fs::path expected = platform::exeDir() / "temp";
    CHECK(MmapImage::defaultFolder() == expected);
    CHECK(MmapImage::resolveFolder({}) == expected);
    CHECK(fs::is_directory(expected));
}

TEST_CASE("create with no folder uses the default scratch folder") {
    MmapImage img;
    REQUIRE(img.create({}, 8, 8));
    fs::path p = img.path();
    CHECK(p.parent_path() == MmapImage::defaultFolder());
    CHECK(fs::exists(p) == kScratchVisibleWhileOpen);
    img.destroy();
    CHECK_FALSE(fs::exists(p));
}

TEST_CASE("chooseFolder creates a writable preferred folder") {
    TempDir dir("mmap");
    fs::path preferred = dir.path() / "nested" / "temp";
    fs::path fallback = dir.path() / "fallback";
    CHECK(MmapImage::chooseFolder(preferred, fallback) == preferred);
    CHECK(fs::is_directory(preferred));
    // The write probe must not be left behind.
    CHECK(fs::is_empty(preferred));
}

TEST_CASE("chooseFolder falls back when the preferred folder can't be created") {
    TempDir dir("mmap");
    // A regular file where a directory would need to be: creation must fail.
    fs::path blocker = dir.path() / "blocker";
    FILE* f = platform::openFile(blocker, "wb");
    REQUIRE(f);
    std::fclose(f);
    fs::path fallback = dir.path() / "fallback";
    CHECK(MmapImage::chooseFolder(blocker / "temp", fallback) == fallback);
}
