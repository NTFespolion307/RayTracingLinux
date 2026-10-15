// Tests for the non-interactive parts of platform.h (dialogs and message
// boxes need a human and are checked manually).
#include <doctest/doctest.h>
#include "platform.h"
#include "test_util.h"
#include <cstring>

namespace fs = std::filesystem;

TEST_CASE("exeDir is the directory holding the test executable") {
    fs::path dir = platform::exeDir();
    REQUIRE_FALSE(dir.empty());
    CHECK(dir.is_absolute());
    CHECK(fs::is_directory(dir));
    bool found = false;
    for (const auto& e : fs::directory_iterator(dir)) {
        std::string stem = e.path().stem().string();
        if (stem == "VkGigaTracerTests") found = true;
    }
    CHECK(found);
}

TEST_CASE("UTF-8 path conversions round-trip non-ASCII names") {
    const std::string utf8 = "dossier_\xC3\xA9_\xE6\x97\xA5\xE6\x9C\xAC/t\xC3\xABst.ppm";
    fs::path p = platform::fromUtf8(utf8);
    CHECK(p.filename() == platform::fromUtf8("t\xC3\xABst.ppm"));
    CHECK(platform::toUtf8(p.filename()) == "t\xC3\xABst.ppm");
    CHECK(platform::toUtf8(platform::fromUtf8(utf8)) == platform::toUtf8(p));
    CHECK(platform::toUtf8(fs::path()).empty());
}

TEST_CASE("openFile writes, appends and reads non-ASCII paths") {
    testutil::TempDir dir("platform");
    fs::path p = dir.path() / platform::fromUtf8("journal_\xC3\xA9\xE6\x97\xA5.txt");

    FILE* f = platform::openFile(p, "wb");
    REQUIRE(f);
    std::fputs("one\n", f);
    std::fclose(f);

    f = platform::openFile(p, "a");
    REQUIRE(f);
    std::fputs("two\n", f);
    std::fclose(f);

    f = platform::openFile(p, "rb");
    REQUIRE(f);
    char buf[32] = {};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    std::string s(buf, n);
    CHECK(s.find("one") == 0);
    CHECK(s.find("two") != std::string::npos);

    CHECK(platform::openFile(dir.path() / "missing" / "x.txt", "rb") == nullptr);
}

TEST_CASE("localTime converts a known timestamp") {
    std::tm tm{};
    // 1971-07-01 00:00 UTC: still 30 Jun / 1 Jul 1971 in every time zone.
    REQUIRE(platform::localTime(std::time_t(86400LL * (365 + 181)), tm));
    CHECK(tm.tm_year == 71);
    CHECK((tm.tm_mon == 5 || tm.tm_mon == 6));
}

TEST_CASE("systemUiFont returns an existing file or nothing") {
    for (bool semibold : { false, true }) {
        fs::path p = platform::systemUiFont(semibold);
        if (!p.empty()) CHECK(fs::is_regular_file(p));
    }
#ifdef _WIN32
    CHECK_FALSE(platform::systemUiFont(false).empty());   // Segoe UI ships with Windows
#endif
}
