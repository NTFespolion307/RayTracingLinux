// Locks in the render option tables and RenderSettings defaults.
#include <doctest/doctest.h>
#include "render_settings.h"
#include <cstring>

TEST_CASE("resolution table is exactly the documented 13 entries") {
    struct Expected { const char* label; uint32_t w, h; };
    const Expected expected[] = {
        { "480p",  854,   480   }, { "720p",  1280,  720   }, { "1080p", 1920,  1080  },
        { "2K",    2560,  1440  }, { "4K",    3840,  2160  }, { "6K",    5760,  3240  },
        { "8K",    7680,  4320  }, { "10K",   9600,  5400  }, { "15K",   14400, 8100  },
        { "20K",   19200, 10800 }, { "40K",   38400, 21600 }, { "60K",   57600, 32400 },
        { "80K",   76800, 43200 },
    };
    REQUIRE(kResolutionCount == 13);
    for (int i = 0; i < kResolutionCount; ++i) {
        CAPTURE(i);
        CHECK(std::strcmp(kResolutions[i].label, expected[i].label) == 0);
        CHECK(kResolutions[i].width == expected[i].w);
        CHECK(kResolutions[i].height == expected[i].h);
    }
}

TEST_CASE("spp, bounce and tile option lists") {
    const int spp[] = { 1, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
    REQUIRE(kSppOptionCount == 12);
    for (int i = 0; i < kSppOptionCount; ++i) CHECK(kSppOptions[i] == spp[i]);

    const int bounces[] = { 16, 32, 48, 64 };
    REQUIRE(kBounceOptionCount == 4);
    for (int i = 0; i < kBounceOptionCount; ++i) CHECK(kBounceOptions[i] == bounces[i]);

    const int tiles[] = { 128, 256, 512 };
    REQUIRE(kTileSizeOptionCount == 3);
    for (int i = 0; i < kTileSizeOptionCount; ++i) CHECK(kTileSizeOptions[i] == tiles[i]);

    CHECK(kJpegMaxSide == 65500u);
}

TEST_CASE("RenderSettings defaults") {
    RenderSettings s;
    CHECK(s.resolutionIndex == 2);
    CHECK(s.width() == 1920);
    CHECK(s.height() == 1080);
    CHECK(s.spp == 16);
    CHECK(s.maxBounces == 16);
    CHECK(s.exposure == 1.0f);
    CHECK(s.fireflyClamp);
    CHECK(s.fireflyClampValue == 64.0f);
    CHECK(s.skyEnabled);
    CHECK(s.skyIntensity == 1.0f);
    CHECK(s.backend == Backend::Auto);
    CHECK(s.tileSize == 256);
    CHECK(s.scratchFolder.empty());
}

TEST_CASE("JPEG availability boundary per resolution") {
    // 60K and below fit in JPEG; 80K does not (README "Saving").
    for (int i = 0; i < kResolutionCount; ++i) {
        CAPTURE(kResolutions[i].label);
        bool fits = kResolutions[i].width <= kJpegMaxSide && kResolutions[i].height <= kJpegMaxSide;
        CHECK(fits == (i != kResolutionCount - 1));
    }
}
