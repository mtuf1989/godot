/**************************************************************************/
/*  test_symphony_room_estimator.cpp                                      */
/*  Suite: [Symphony][Spatial][RoomEstimator] — ray-fan RT60 (3.3/3.4).   */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_room_estimator)

#include "modules/symphony/spatial/acoustic_material.h"
#include "modules/symphony/spatial/room_estimator.h"

namespace TestSymphonyRoomEstimator {

// RoomEstimator::compute() is exercised with a SYNTHETIC raycast functor that
// models an axis-aligned box room centred on the origin — no PhysicsServer3D.
// estimate() is the thin physics wrapper around this template.

// An axis-aligned box room [-half, +half] on each axis. Rays cast from the
// centre hit the box walls; the hit material is uniform. Optionally an "opening"
// removes a cone of directions (rays with dir.x > open_cos escape — a window on
// the +x wall) so those rays report no hit.
struct BoxRoom {
	Vector3 half = Vector3(5, 3, 5);
	AcousticMaterial *mat = nullptr;
	bool has_opening = false;
	float open_cos = 0.9f; // rays with dir.x >= open_cos escape through the window

	bool raycast(const Vector3 &from, const Vector3 &to, Vector3 &r_pos, AcousticMaterial **r_mat) const {
		Vector3 dir = (to - from).normalized();
		if (has_opening && dir.x >= open_cos) {
			return false; // escaped through the window
		}
		// Slab intersection with the box faces; find nearest positive t.
		float best_t = INFINITY;
		for (int axis = 0; axis < 3; axis++) {
			float d = dir[axis];
			if (Math::abs(d) < 1e-6f) {
				continue;
			}
			for (int s = -1; s <= 1; s += 2) {
				float plane = (float)s * half[axis];
				float t = (plane - from[axis]) / d;
				if (t <= 1e-4f) {
					continue;
				}
				// Verify the hit lies within the face bounds on the other axes.
				Vector3 p = from + dir * t;
				bool inside = true;
				for (int a2 = 0; a2 < 3; a2++) {
					if (a2 == axis) {
						continue;
					}
					if (Math::abs(p[a2]) > half[a2] + 1e-3f) {
						inside = false;
						break;
					}
				}
				if (inside && t < best_t) {
					best_t = t;
				}
			}
		}
		if (!Math::is_inf(best_t)) {
			r_pos = from + dir * best_t;
			*r_mat = mat;
			return true;
		}
		return false;
	}
};

static RoomEstimator::Result run(const BoxRoom &room, int ray_count = 64, float max_distance = 100.0f) {
	RoomEstimator::Config cfg;
	cfg.ray_count = ray_count;
	cfg.max_distance = max_distance;
	return RoomEstimator::compute(Vector3(0, 0, 0), cfg,
			[&](const Vector3 &f, const Vector3 &t, Vector3 &p, AcousticMaterial **m) { return room.raycast(f, t, p, m); });
}

// --- Sealed room --------------------------------------------------------

TEST_CASE("[Symphony][Spatial][RoomEstimator] Sealed concrete cube: finite RT60, near-zero openness") {
	Ref<AcousticMaterial> concrete = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CONCRETE);
	BoxRoom room;
	room.half = Vector3(5, 5, 5);
	room.mat = concrete.ptr();

	RoomEstimator::Result r = run(room);
	CHECK(r.rays_hit > 0);
	CHECK(r.openness == doctest::Approx(0.0f).epsilon(0.001)); // fully enclosed
	CHECK(r.volume > 0.0f);
	CHECK(r.rt60 > 0.0f); // reflective room reverberates
}

// --- Foam vs concrete: absorption shortens RT60 ------------------------

TEST_CASE("[Symphony][Spatial][RoomEstimator] Foam cube RT60 much shorter than concrete") {
	Ref<AcousticMaterial> concrete = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CONCRETE);
	Ref<AcousticMaterial> foam = AcousticMaterial::create_preset(AcousticMaterial::PRESET_ACOUSTIC_FOAM);

	BoxRoom hard;
	hard.half = Vector3(5, 5, 5);
	hard.mat = concrete.ptr();
	BoxRoom soft = hard;
	soft.mat = foam.ptr();

	float rt_hard = run(hard).rt60;
	float rt_soft = run(soft).rt60;
	CHECK(rt_soft < rt_hard);
}

// --- REGRESSION: a window SHORTENS RT60 --------------------------------

TEST_CASE("[Symphony][Spatial][RoomEstimator] REGRESSION: one window yields RT60 shorter than sealed") {
	Ref<AcousticMaterial> concrete = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CONCRETE);

	BoxRoom sealed;
	sealed.half = Vector3(5, 5, 5);
	sealed.mat = concrete.ptr();

	BoxRoom windowed = sealed;
	windowed.has_opening = true; // a cone of rays escapes through a +x window

	RoomEstimator::Result r_sealed = run(sealed);
	RoomEstimator::Result r_window = run(windowed);

	CHECK(r_window.openness > r_sealed.openness); // the window shows up as openness
	// The opening is a perfect absorber AT THE WALL (Phase 3.3), so energy leaves
	// and RT60 drops — it must NOT inflate the volume integral / lengthen RT60.
	CHECK(r_window.rt60 < r_sealed.rt60);
}

// --- All escaped: open sky ---------------------------------------------

TEST_CASE("[Symphony][Spatial][RoomEstimator] All rays escape → openness 1, RT60 0") {
	BoxRoom nothing; // no material, but crucially:
	nothing.has_opening = true;
	nothing.open_cos = -2.0f; // every direction escapes (dir.x >= -2 is always true)

	RoomEstimator::Result r = run(nothing);
	CHECK(r.openness == doctest::Approx(1.0f));
	CHECK(r.rt60 == doctest::Approx(0.0f));
}

// --- Eyring engages for highly absorptive rooms ------------------------

TEST_CASE("[Symphony][Spatial][RoomEstimator] High absorption engages Eyring (finite, short RT60)") {
	Ref<AcousticMaterial> foam = AcousticMaterial::create_preset(AcousticMaterial::PRESET_ACOUSTIC_FOAM);
	BoxRoom room;
	room.half = Vector3(4, 4, 4);
	room.mat = foam.ptr();

	RoomEstimator::Result r = run(room);
	// Foam is a total absorber → mean_absorption clamps high, Eyring path taken.
	CHECK(r.mean_absorption >= 0.3f); // above eyring_threshold
	CHECK(r.rt60 >= 0.0f);
	CHECK(r.rt60 < 0.5f); // very dead room
}

// --- Solid-angle renormalization with ignore_floor ---------------------

TEST_CASE("[Symphony][Spatial][RoomEstimator] Partial fan (ignore_floor) stays self-consistent") {
	Ref<AcousticMaterial> concrete = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CONCRETE);
	BoxRoom room;
	room.half = Vector3(5, 3, 5);
	room.mat = concrete.ptr();

	RoomEstimator::Config full_cfg;
	full_cfg.ray_count = 64;
	full_cfg.ignore_floor = false;
	RoomEstimator::Result full = RoomEstimator::compute(Vector3(0, 0, 0), full_cfg,
			[&](const Vector3 &f, const Vector3 &t, Vector3 &p, AcousticMaterial **m) { return room.raycast(f, t, p, m); });

	RoomEstimator::Config floorless_cfg = full_cfg;
	floorless_cfg.ignore_floor = true;
	RoomEstimator::Result floorless = RoomEstimator::compute(Vector3(0, 0, 0), floorless_cfg,
			[&](const Vector3 &f, const Vector3 &t, Vector3 &p, AcousticMaterial **m) { return room.raycast(f, t, p, m); });

	// Fewer active rays, but the 4π/active_rays renormalization keeps the volume
	// estimate in the same ballpark (not off by the fraction of filtered rays).
	CHECK(floorless.rays_cast < full.rays_cast);
	CHECK(floorless.volume > 0.0f);
	CHECK(floorless.volume == doctest::Approx(full.volume).epsilon(0.5)); // same order
}

// --- Degenerate configs -------------------------------------------------

TEST_CASE("[Symphony][Spatial][RoomEstimator] Zero rays → empty result") {
	BoxRoom room;
	Ref<AcousticMaterial> concrete = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CONCRETE);
	room.mat = concrete.ptr();
	RoomEstimator::Result r = run(room, 0);
	CHECK(r.rt60 == doctest::Approx(0.0f));
	CHECK(r.volume == doctest::Approx(0.0f));
}

TEST_CASE("[Symphony][Spatial][RoomEstimator] Null physics space → empty result") {
	RoomEstimator::Config cfg;
	RoomEstimator::Result r = RoomEstimator::estimate(nullptr, Vector3(0, 0, 0), Vector<RID>(), cfg);
	CHECK(r.rt60 == doctest::Approx(0.0f));
	CHECK(r.rays_cast == 0);
}

} // namespace TestSymphonyRoomEstimator
