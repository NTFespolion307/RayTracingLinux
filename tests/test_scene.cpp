// Snapshot of the procedural demo scene so refactors can't silently change it.
#include <doctest/doctest.h>
#include "scene.h"

TEST_CASE("demo scene geometry, materials and lights") {
    Scene s = buildDemoScene();
    CHECK(s.name == "Demo Scene");

    // 5 room quads + 1 light quad + 2 icospheres (20 * 4^4 faces) + 1 box.
    const size_t expectedTris = 5 * 2 + 1 * 2 + 2 * 5120 + 12;
    CHECK(s.triangles.size() == expectedTris);
    CHECK(s.vertices.size() == expectedTris * 3);

    REQUIRE(s.materials.size() == 6);
    const uint32_t types[] = { MAT_LAMBERT, MAT_LAMBERT, MAT_LAMBERT, MAT_EMISSIVE, MAT_METAL, MAT_GLASS };
    for (int i = 0; i < 6; ++i) CHECK((uint32_t)s.materials[i].params.x == types[i]);
    CHECK(s.materials[3].emission.x == doctest::Approx(18.0f));
    CHECK(s.materials[5].params.z == doctest::Approx(1.5f));

    // The ceiling light quad is the only emitter: 2 triangles, 0.7 x 0.7 total.
    REQUIRE(s.lights.size() == 2);
    CHECK(s.lights[0].emission.w + s.lights[1].emission.w == doctest::Approx(0.49f));

    CHECK(s.bounds.lo.x == doctest::Approx(-1.0f));
    CHECK(s.bounds.lo.y == doctest::Approx(0.0f));
    CHECK(s.bounds.lo.z == doctest::Approx(-1.0f));
    CHECK(s.bounds.hi.x == doctest::Approx(1.0f));
    CHECK(s.bounds.hi.y == doctest::Approx(2.0f));
    CHECK(s.bounds.hi.z == doctest::Approx(1.0f));

    CHECK(s.camera.fovYDegrees == doctest::Approx(40.0f));
    CHECK(s.camera.position.x == doctest::Approx(0.0f));
    CHECK(s.camera.position.y == doctest::Approx(1.0f));
    CHECK(s.camera.position.z == doctest::Approx(4.2f));
    CHECK(s.camera.target.y == doctest::Approx(1.0f));
}

TEST_CASE("every demo triangle references valid vertices and materials") {
    Scene s = buildDemoScene();
    for (const GPUTriangle& t : s.triangles) {
        CHECK(t.v0 < s.vertices.size());
        CHECK(t.v1 < s.vertices.size());
        CHECK(t.v2 < s.vertices.size());
        CHECK(t.material < s.materials.size());
    }
}
