/**************************************************************************/
/*  test_symphony_occlusion.cpp                                           */
/*  Suite: [Symphony][Spatial][Occlusion] — forward-march solver (2.2).   */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_occlusion)

#include "modules/symphony/spatial/acoustic_material.h"
#include "modules/symphony/spatial/occlusion_solver.h"

namespace TestSymphonyOcclusion {

// The direct-path occlusion math is verified through OcclusionSolver::compute()
// with a SYNTHETIC raycast functor that returns scripted hits + materials — no
// live PhysicsServer3D, no SceneTree (Phase 1.2). solve() is a thin physics
// wrapper around exactly this template.
//
// The functor signature is:
//   bool fn(from, to, r_pos, r_mat, r_barrier_id)

// A scripted wall along the x axis: a solid slab spans [x_lo, x_hi]. A forward
// march from source→listener hits its ENTRY face, then (after advancing past it)
// its EXIT face — two hits for one solid wall. Both faces share barrier_id so
// transmission is applied once per wall.
struct WallSet {
	struct Wall {
		float x_lo;
		float x_hi;
		AcousticMaterial *mat; // may be nullptr (untagged)
		uint64_t barrier_id = 0; // stable key; 0 → assigned from index+1
	};
	LocalVector<Wall> walls;
	bool skip_start_inside = false; // Godot Physics: ignore shape when from is inside

	// Return the nearest face strictly ahead of `from` toward +x, if any.
	bool raycast(const Vector3 &from, const Vector3 &to, Vector3 &r_pos, AcousticMaterial **r_mat, uint64_t &r_barrier_id) const {
		if (to.x <= from.x) {
			return false; // this synthetic world only occludes along +x
		}
		float best = to.x;
		bool hit = false;
		AcousticMaterial *hit_mat = nullptr;
		uint64_t hit_id = 0;
		for (uint32_t i = 0; i < walls.size(); i++) {
			const Wall &w = walls[i];
			const uint64_t id = w.barrier_id != 0 ? w.barrier_id : (uint64_t)(i + 1);
			if (skip_start_inside && from.x > w.x_lo + 1e-4f && from.x < w.x_hi - 1e-4f) {
				continue; // start inside solid → Godot Physics skips this shape
			}
			// Entry face.
			if (w.x_lo > from.x + 1e-4f && w.x_lo < best) {
				best = w.x_lo;
				hit = true;
				hit_mat = w.mat;
				hit_id = id;
			}
			// Exit face.
			if (w.x_hi > from.x + 1e-4f && w.x_hi < best) {
				best = w.x_hi;
				hit = true;
				hit_mat = w.mat;
				hit_id = id;
			}
		}
		if (!hit) {
			return false;
		}
		r_pos = Vector3(best, from.y, from.z);
		*r_mat = hit_mat;
		r_barrier_id = hit_id;
		return true;
	}
};

#define OCC_RAY(world) \
	[&](const Vector3 &f, const Vector3 &t, Vector3 &p, AcousticMaterial **m, uint64_t &id) { return (world).raycast(f, t, p, m, id); }

static Ref<AcousticMaterial> concrete() {
	return AcousticMaterial::create_preset(AcousticMaterial::PRESET_CONCRETE);
}

// --- No occlusion -------------------------------------------------------

TEST_CASE("[Symphony][Spatial][Occlusion] Clear path → full transmission, zero occlusion") {
	WallSet world; // no walls
	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(10, 0, 0), cfg, OCC_RAY(world));
	CHECK(r.hit_count == 0);
	CHECK(r.transmission[0] == doctest::Approx(1.0f));
	CHECK(r.transmission[1] == doctest::Approx(1.0f));
	CHECK(r.transmission[2] == doctest::Approx(1.0f));
	CHECK(r.occlusion == doctest::Approx(0.0f));
}

// --- Thin plane (1 hit) -------------------------------------------------

TEST_CASE("[Symphony][Spatial][Occlusion] Thin plane (1 hit) → transmission = t") {
	Ref<AcousticMaterial> mat = concrete();
	WallSet world;
	// A zero-thickness plane: entry face only (x_hi coincident so exit==entry is
	// skipped by the strict-ahead test after the offset advance).
	world.walls.push_back({ 5.0f, 5.0f, mat.ptr() });

	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(10, 0, 0), cfg, OCC_RAY(world));
	CHECK(r.hit_count == 1);
	// One hit → transmission equals the material value directly.
	CHECK(r.transmission[1] == doctest::Approx(mat->get_transmission_mid()));
	CHECK(r.transmission[2] == doctest::Approx(mat->get_transmission_high()));
}

// --- Solid slab regression: 2 hits same barrier → t, NOT t² ------------

TEST_CASE("[Symphony][Spatial][Occlusion] REGRESSION: solid slab (2 hits) yields t, not t-squared") {
	Ref<AcousticMaterial> mat = concrete();
	WallSet world;
	world.walls.push_back({ 4.0f, 6.0f, mat.ptr() }); // one solid wall, 2 faces, one barrier_id

	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(12, 0, 0), cfg, OCC_RAY(world));
	REQUIRE(r.hit_count == 2); // entry + exit
	// Per-barrier: one solid wall = one attenuation (t), not t².
	CHECK(r.transmission[1] == doctest::Approx(mat->get_transmission_mid()).epsilon(0.001));
	CHECK(r.transmission[2] == doctest::Approx(mat->get_transmission_high()).epsilon(0.001));
	CHECK(r.transmission[1] > mat->get_transmission_mid() * mat->get_transmission_mid());
}

// --- Two solid walls: 4 hits → t² --------------------------------------

TEST_CASE("[Symphony][Spatial][Occlusion] Two solid walls (4 hits) → t-squared") {
	Ref<AcousticMaterial> mat = concrete();
	WallSet world;
	world.walls.push_back({ 3.0f, 4.0f, mat.ptr() });
	world.walls.push_back({ 7.0f, 8.0f, mat.ptr() });

	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(12, 0, 0), cfg, OCC_RAY(world));
	REQUIRE(r.hit_count == 4); // 2 walls × 2 faces
	const float t = mat->get_transmission_mid();
	CHECK(r.transmission[1] == doctest::Approx(t * t).epsilon(0.001));
}

// --- Godot Physics start-inside: 1 hit/wall still → t² -----------------

TEST_CASE("[Symphony][Spatial][Occlusion] REGRESSION: start-inside skip still attenuates per wall") {
	Ref<AcousticMaterial> mat = concrete();
	WallSet world;
	world.skip_start_inside = true;
	world.walls.push_back({ 3.0f, 4.0f, mat.ptr() });
	world.walls.push_back({ 7.0f, 8.0f, mat.ptr() });

	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(12, 0, 0), cfg, OCC_RAY(world));
	// Entry only per wall (exit skipped because next ray starts inside).
	REQUIRE(r.hit_count == 2);
	const float t = mat->get_transmission_mid();
	// Must be t² — NOT the old sqrt(t*t)=t under-attenuation.
	CHECK(r.transmission[1] == doctest::Approx(t * t).epsilon(0.001));
	CHECK(r.transmission[1] < t * 0.9f);
}

// --- Total absorption ---------------------------------------------------

TEST_CASE("[Symphony][Spatial][Occlusion] Total-absorption wall mutes and stops the march") {
	Ref<AcousticMaterial> foam = AcousticMaterial::create_preset(AcousticMaterial::PRESET_ACOUSTIC_FOAM);
	REQUIRE(foam->get_total_absorption());
	WallSet world;
	world.walls.push_back({ 5.0f, 6.0f, foam.ptr() });
	// A second wall behind it must never be reached (the march stops).
	world.walls.push_back({ 8.0f, 9.0f, foam.ptr() });

	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(12, 0, 0), cfg, OCC_RAY(world));
	CHECK(r.total_absorption_hit);
	CHECK(r.transmission[0] == doctest::Approx(0.0f));
	CHECK(r.transmission[1] == doctest::Approx(0.0f));
	CHECK(r.transmission[2] == doctest::Approx(0.0f));
	CHECK(r.occlusion == doctest::Approx(1.0f));
	CHECK(r.hit_count == 1); // stopped at the first (total) wall
	CHECK(r.total_absorption_speed == doctest::Approx(foam->get_total_absorption_transition_speed()));
}

// --- Untagged collider uses fallback transmission ----------------------

TEST_CASE("[Symphony][Spatial][Occlusion] Untagged collider uses config fallback") {
	WallSet world;
	world.walls.push_back({ 5.0f, 5.0f, nullptr }); // thin, untagged

	OcclusionSolver::Config cfg;
	cfg.fallback_transmission_mid = 0.05f;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(10, 0, 0), cfg, OCC_RAY(world));
	CHECK(r.hit_count == 1);
	CHECK(r.transmission[1] == doctest::Approx(cfg.fallback_transmission_mid));
}

// --- max_hits cap -------------------------------------------------------

TEST_CASE("[Symphony][Spatial][Occlusion] max_hits caps the march") {
	Ref<AcousticMaterial> mat = concrete();
	WallSet world;
	for (int i = 1; i <= 10; i++) {
		world.walls.push_back({ (float)i, (float)i, mat.ptr() });
	}
	OcclusionSolver::Config cfg;
	cfg.max_hits = 3;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(20, 0, 0), cfg, OCC_RAY(world));
	CHECK(r.hit_count <= cfg.max_hits);
}

// --- Hit beyond the listener is rejected -------------------------------

TEST_CASE("[Symphony][Spatial][Occlusion] Hit beyond the listener is ignored") {
	Ref<AcousticMaterial> mat = concrete();
	WallSet world;
	world.walls.push_back({ 50.0f, 51.0f, mat.ptr() }); // far past the listener at x=10

	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(0, 0, 0), Vector3(10, 0, 0), cfg, OCC_RAY(world));
	CHECK(r.hit_count == 0);
	CHECK(r.occlusion == doctest::Approx(0.0f));
}

// --- Degenerate: coincident source/listener ----------------------------

TEST_CASE("[Symphony][Spatial][Occlusion] Coincident source and listener → clear") {
	WallSet world;
	OcclusionSolver::Config cfg;
	OcclusionSolver::Result r = OcclusionSolver::compute(
			Vector3(1, 2, 3), Vector3(1, 2, 3), cfg, OCC_RAY(world));
	CHECK(r.occlusion == doctest::Approx(0.0f));
	CHECK(r.hit_count == 0);
}

} // namespace TestSymphonyOcclusion
