// Structural invariants of the flattened BVH, plus a CPU traversal (mirroring
// the compute shader's layout) checked against brute-force intersection.
#include <doctest/doctest.h>
#include "bvh.h"
#include <algorithm>
#include <cstring>
#include <random>
#include <tuple>

namespace {

uint32_t asUint(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

vec3 pos(const std::vector<GPUVertex>& v, uint32_t i) { return { v[i].pos.x, v[i].pos.y, v[i].pos.z }; }

// Moller-Trumbore; returns t > eps or a negative value on miss.
float intersect(vec3 o, vec3 d, vec3 a, vec3 b, vec3 c) {
    const float eps = 1e-7f;
    vec3 e1 = b - a, e2 = c - a;
    vec3 p = cross(d, e2);
    float det = dot(e1, p);
    if (std::fabs(det) < eps) return -1.0f;
    float inv = 1.0f / det;
    vec3 s = o - a;
    float u = dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return -1.0f;
    vec3 q = cross(s, e1);
    float v = dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return -1.0f;
    float t = dot(e2, q) * inv;
    return t > 1e-5f ? t : -1.0f;
}

bool hitBox(vec3 o, vec3 invD, vec3 lo, vec3 hi, float tMax) {
    float t0 = 0.0f, t1 = tMax;
    for (int a = 0; a < 3; ++a) {
        float tn = (lo[a] - o[a]) * invD[a];
        float tf = (hi[a] - o[a]) * invD[a];
        if (tn > tf) std::swap(tn, tf);
        t0 = std::max(t0, tn);
        t1 = std::min(t1, tf);
        if (t0 > t1 * 1.0000001f + 1e-6f) return false;
    }
    return true;
}

float traverse(const BVHResult& bvh, const std::vector<GPUVertex>& v, vec3 o, vec3 d) {
    vec3 invD{ 1.0f / d.x, 1.0f / d.y, 1.0f / d.z };
    float best = 1e30f;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const GPUBVHNode& n = bvh.nodes[stack[--sp]];
        vec3 lo{ n.lo.x, n.lo.y, n.lo.z }, hi{ n.hi.x, n.hi.y, n.hi.z };
        if (!hitBox(o, invD, lo, hi, best)) continue;
        uint32_t first = asUint(n.lo.w), count = asUint(n.hi.w);
        if (count > 0) {
            for (uint32_t i = first; i < first + count; ++i) {
                const GPUTriangle& t = bvh.orderedTriangles[i];
                float h = intersect(o, d, pos(v, t.v0), pos(v, t.v1), pos(v, t.v2));
                if (h > 0.0f && h < best) best = h;
            }
        } else {
            REQUIRE(sp + 2 <= 64);
            stack[sp++] = first;
            stack[sp++] = first + 1;
        }
    }
    return best;
}

float bruteForce(const std::vector<GPUVertex>& v, const std::vector<GPUTriangle>& tris, vec3 o, vec3 d) {
    float best = 1e30f;
    for (const GPUTriangle& t : tris) {
        float h = intersect(o, d, pos(v, t.v0), pos(v, t.v1), pos(v, t.v2));
        if (h > 0.0f && h < best) best = h;
    }
    return best;
}

bool contains(vec3 lo, vec3 hi, vec3 p) {
    const float e = 1e-5f;
    return p.x >= lo.x - e && p.y >= lo.y - e && p.z >= lo.z - e &&
           p.x <= hi.x + e && p.y <= hi.y + e && p.z <= hi.z + e;
}

// Walks the tree checking child/leaf containment; returns how many times each
// orderedTriangles slot is referenced by a leaf.
void checkStructure(const BVHResult& bvh, const std::vector<GPUVertex>& v) {
    std::vector<int> refs(bvh.orderedTriangles.size(), 0);
    std::vector<uint32_t> stack{ 0 };
    size_t visited = 0;
    while (!stack.empty()) {
        uint32_t idx = stack.back();
        stack.pop_back();
        REQUIRE(idx < bvh.nodes.size());
        ++visited;
        const GPUBVHNode& n = bvh.nodes[idx];
        vec3 lo{ n.lo.x, n.lo.y, n.lo.z }, hi{ n.hi.x, n.hi.y, n.hi.z };
        uint32_t first = asUint(n.lo.w), count = asUint(n.hi.w);
        if (count > 0) {
            REQUIRE(first + count <= bvh.orderedTriangles.size());
            for (uint32_t i = first; i < first + count; ++i) {
                ++refs[i];
                const GPUTriangle& t = bvh.orderedTriangles[i];
                CHECK(contains(lo, hi, pos(v, t.v0)));
                CHECK(contains(lo, hi, pos(v, t.v1)));
                CHECK(contains(lo, hi, pos(v, t.v2)));
            }
        } else {
            REQUIRE(first + 1 < bvh.nodes.size());
            for (uint32_t c = first; c <= first + 1; ++c) {
                const GPUBVHNode& ch = bvh.nodes[c];
                CHECK(contains(lo, hi, { ch.lo.x, ch.lo.y, ch.lo.z }));
                CHECK(contains(lo, hi, { ch.hi.x, ch.hi.y, ch.hi.z }));
                stack.push_back(c);
            }
        }
    }
    CHECK(visited == bvh.nodes.size());
    for (int r : refs) CHECK(r == 1);
}

bool sameTriangleMultiset(std::vector<GPUTriangle> a, std::vector<GPUTriangle> b) {
    auto key = [](const GPUTriangle& t) { return std::make_tuple(t.v0, t.v1, t.v2, t.material); };
    auto less = [&](const GPUTriangle& x, const GPUTriangle& y) { return key(x) < key(y); };
    std::sort(a.begin(), a.end(), less);
    std::sort(b.begin(), b.end(), less);
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [&](const GPUTriangle& x, const GPUTriangle& y) { return key(x) == key(y); });
}

} // namespace

TEST_CASE("empty input yields a single empty leaf") {
    BVHResult r = buildBVH({}, {});
    REQUIRE(r.nodes.size() == 1);
    CHECK(asUint(r.nodes[0].hi.w) == 0);
    CHECK(r.orderedTriangles.empty());
}

TEST_CASE("demo scene BVH is a valid permutation with containing bounds") {
    Scene s = buildDemoScene();
    BVHResult r = buildBVH(s.vertices, s.triangles);
    CHECK(r.orderedTriangles.size() == s.triangles.size());
    CHECK(r.nodes.size() <= s.triangles.size() * 2);
    CHECK(sameTriangleMultiset(r.orderedTriangles, s.triangles));
    checkStructure(r, s.vertices);
}

TEST_CASE("BVH traversal matches brute force on the demo scene") {
    Scene s = buildDemoScene();
    BVHResult r = buildBVH(s.vertices, s.triangles);

    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> U(-1.0f, 1.0f);
    int hits = 0;
    for (int i = 0; i < 2000; ++i) {
        vec3 o{ U(rng) * 0.95f, 1.0f + U(rng) * 0.95f, U(rng) * 0.95f + 0.5f };
        vec3 d = normalize({ U(rng), U(rng), U(rng) });
        if (length(d) < 0.5f) continue;
        float tb = traverse(r, s.vertices, o, d);
        float tr = bruteForce(s.vertices, s.triangles, o, d);
        CAPTURE(i);
        CHECK(tb == doctest::Approx(tr).epsilon(1e-5));
        if (tr < 1e29f) ++hits;
    }
    CHECK(hits > 1000);
}

TEST_CASE("BVH handles degenerate input (all centroids equal)") {
    std::vector<GPUVertex> v;
    std::vector<GPUTriangle> t;
    for (uint32_t i = 0; i < 10; ++i) {
        uint32_t b = (uint32_t)v.size();
        v.push_back({ vec4(0, 0, 0, 0), vec4(0, 0, 1, 0) });
        v.push_back({ vec4(1, 0, 0, 0), vec4(0, 0, 1, 0) });
        v.push_back({ vec4(0, 1, 0, 0), vec4(0, 0, 1, 0) });
        t.push_back({ b, b + 1, b + 2, i });
    }
    BVHResult r = buildBVH(v, t);
    CHECK(sameTriangleMultiset(r.orderedTriangles, t));
    checkStructure(r, v);
}
