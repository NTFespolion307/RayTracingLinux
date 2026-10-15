// Locks in OBJ/MTL import: material mapping, triangulation, default material,
// smooth normals, error handling, and non-ASCII directories.
#include <doctest/doctest.h>
#include "obj_loader.h"
#include "test_util.h"
#include <cmath>

namespace fs = std::filesystem;
using testutil::TempDir;

namespace {

fs::path dataFile(const char* name) { return fs::path(VKGT_TEST_DATA_DIR) / name; }

void checkVec(const vec4& v, float x, float y, float z) {
    CHECK(v.x == doctest::Approx(x));
    CHECK(v.y == doctest::Approx(y));
    CHECK(v.z == doctest::Approx(z));
}

void checkMaterialsFixture(const ObjLoadResult& r) {
    REQUIRE(r.ok);
    const Scene& s = r.scene;

    // 4 MTL materials in file order, then the trailing default.
    REQUIRE(s.materials.size() == 5);

    const GPUMaterial& lambert = s.materials[0];
    CHECK((uint32_t)lambert.params.x == MAT_LAMBERT);
    checkVec(lambert.albedo, 0.5f, 0.25f, 0.125f);
    checkVec(lambert.emission, 0, 0, 0);
    CHECK(lambert.params.y == doctest::Approx(1.0f));
    CHECK(lambert.params.z == doctest::Approx(1.0f));

    const GPUMaterial& metal = s.materials[1];
    CHECK((uint32_t)metal.params.x == MAT_METAL);
    checkVec(metal.albedo, 0.9f, 0.8f, 0.7f);                       // Ks wins over Kd
    CHECK(metal.params.y == doctest::Approx(std::sqrt(2.0f / 102.0f))); // roughness from Ns

    const GPUMaterial& glass = s.materials[2];
    CHECK((uint32_t)glass.params.x == MAT_GLASS);
    checkVec(glass.albedo, 0.9f, 0.95f, 1.0f);
    CHECK(glass.params.y == doctest::Approx(0.0f));
    CHECK(glass.params.z == doctest::Approx(1.5f));

    const GPUMaterial& light = s.materials[3];
    CHECK((uint32_t)light.params.x == MAT_EMISSIVE);
    checkVec(light.emission, 5, 4, 3);
    checkVec(light.albedo, 0, 0, 0);

    const GPUMaterial& def = s.materials[4];
    CHECK((uint32_t)def.params.x == MAT_LAMBERT);
    checkVec(def.albedo, 0.7f, 0.7f, 0.7f);

    // 1 loose triangle + 4 quads triangulated.
    REQUIRE(s.triangles.size() == 9);
    CHECK(s.triangles[0].material == 4);
    const uint32_t quadMats[] = { 0, 0, 1, 1, 2, 2, 3, 3 };
    for (int i = 0; i < 8; ++i) CHECK(s.triangles[1 + i].material == quadMats[i]);

    // Vertices are deduplicated per (position, normal) pair.
    CHECK(s.vertices.size() == 19);
    for (const GPUVertex& v : s.vertices) checkVec(v.normal, 0, 0, 1);

    // The two emissive triangles become NEE lights with area 0.5 each.
    REQUIRE(s.lights.size() == 2);
    for (const GPULight& L : s.lights) {
        checkVec(L.emission, 5, 4, 3);
        CHECK(L.emission.w == doctest::Approx(0.5f));
    }

    CHECK(s.bounds.lo.x == doctest::Approx(0.0f));
    CHECK(s.bounds.hi.x == doctest::Approx(7.0f));
    CHECK(s.bounds.hi.y == doctest::Approx(4.0f));
}

} // namespace

TEST_CASE("materials fixture maps every MTL material kind") {
    ObjLoadResult r = loadObj(dataFile("materials.obj").wstring());
    checkMaterialsFixture(r);
    CHECK(r.scene.name == "materials.obj");
}

TEST_CASE("OBJ without normals gets smoothed face normals") {
    ObjLoadResult r = loadObj(dataFile("nonormals.obj").wstring());
    REQUIRE(r.ok);
    CHECK(r.scene.triangles.size() == 2);
    CHECK(r.scene.vertices.size() == 4);
    for (const GPUVertex& v : r.scene.vertices) checkVec(v.normal, 0, 1, 0);
    // No MTL: only the default material.
    CHECK(r.scene.materials.size() == 1);
    CHECK(r.scene.lights.empty());
}

TEST_CASE("missing MTL is not fatal") {
    ObjLoadResult r = loadObj(dataFile("missing_mtl.obj").wstring());
    REQUIRE(r.ok);
    REQUIRE(r.scene.triangles.size() == 1);
    REQUIRE(r.scene.materials.size() == 1);
    CHECK(r.scene.triangles[0].material == 0);
}

TEST_CASE("nonexistent OBJ reports an error") {
    ObjLoadResult r = loadObj((fs::path(VKGT_TEST_DATA_DIR) / "nope.obj").wstring());
    CHECK_FALSE(r.ok);
    CHECK(r.message == "Could not open the OBJ file.");
}

TEST_CASE("OBJ with no faces reports an error") {
    ObjLoadResult r = loadObj(dataFile("empty.obj").wstring());
    CHECK_FALSE(r.ok);
    CHECK(r.message == "The OBJ contained no triangles.");
}

TEST_CASE("OBJ and MTL load from a non-ASCII directory") {
    TempDir tmp("obj");
    fs::path dir = tmp.path() / fs::path(u8"dossier_é_日本");
    fs::create_directories(dir);
    fs::copy_file(dataFile("materials.obj"), dir / "materials.obj");
    fs::copy_file(dataFile("materials.mtl"), dir / "materials.mtl");

    ObjLoadResult r = loadObj((dir / "materials.obj").wstring());
    checkMaterialsFixture(r);
}
