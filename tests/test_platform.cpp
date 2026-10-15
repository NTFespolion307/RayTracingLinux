// Tests for the non-interactive parts of platform.h (dialogs and message
// boxes need a human and are checked manually).
#include <doctest/doctest.h>
#include "platform.h"

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
