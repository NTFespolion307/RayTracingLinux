// The GPU-facing structs are uploaded byte-for-byte and must match the
// std430/std140 layouts in shaders/. Lock their sizes and key offsets so a
// different compiler (GCC vs MSVC) can't silently change them.
#include <doctest/doctest.h>
#include "scene.h"
#include <cstddef>

static_assert(sizeof(vec4) == 16, "vec4 must be 16 bytes");
static_assert(sizeof(GPUVertex) == 32, "GPUVertex layout");
static_assert(sizeof(GPUTriangle) == 16, "GPUTriangle layout");
static_assert(sizeof(GPUMaterial) == 48, "GPUMaterial layout");
static_assert(sizeof(GPUBVHNode) == 32, "GPUBVHNode layout");
static_assert(sizeof(GPULight) == 64, "GPULight layout");
static_assert(sizeof(GPUParams) == 112, "GPUParams layout");
static_assert(sizeof(GPUPushConstants) == 32, "GPUPushConstants layout");

TEST_CASE("GPU struct offsets") {
    CHECK(offsetof(GPUVertex, normal) == 16);
    CHECK(offsetof(GPUTriangle, material) == 12);
    CHECK(offsetof(GPUMaterial, emission) == 16);
    CHECK(offsetof(GPUMaterial, params) == 32);
    CHECK(offsetof(GPUBVHNode, hi) == 16);
    CHECK(offsetof(GPULight, emission) == 48);
    CHECK(offsetof(GPUParams, sky1) == 80);
    CHECK(offsetof(GPUParams, numLights) == 96);
    CHECK(offsetof(GPUParams, imageHeight) == 108);
    CHECK(offsetof(GPUPushConstants, sampleBase) == 16);
    CHECK(offsetof(GPUPushConstants, pad) == 28);
}
