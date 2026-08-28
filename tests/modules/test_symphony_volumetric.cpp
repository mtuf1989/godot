/**************************************************************************/
/*  test_symphony_volumetric.cpp                                          */
/*  Suite: [Symphony][Spatial][Volumetric] — volumetric occlusion (T12).  */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_volumetric)

#include "modules/symphony/runtime/sound_event.h"
#include "modules/symphony/spatial/occlusion_solver.h"
#include "modules/symphony/spatial/spatial_graph_wrapper.h"

namespace TestSymphonyVolumetric {

// The volumetric-occlusion math is verified through OcclusionSolver::
// compute_volumetric() with a synthetic line-of-sight predicate, decoupling
// the graduated-occlusion logic from a live PhysicsServer3D. solve_volumetric()
// is a thin physics wrapper around exactly this function.

// --- SoundEvent field ---------------------------------------------------

TEST_CASE("[Symphony][Spatial][Volumetric] SoundEvent source_radius round-trips") {
	Ref<SoundEvent> event;
	event.instantiate();
	CHECK(event->get_source_radius() == doctest::Approx(0.0f)); // point by default
	event->set_source_radius(2.5f);
	CHECK(event->get_source_radius() == doctest::Approx(2.5f));
}

// --- Volume sample generator -------------------------------------------

TEST_CASE("[Symphony][Spatial][Volumetric] Volume samples fill the unit sphere") {
	Vector<Vector3> offsets;
	OcclusionSolver::generate_volume_samples(64, offsets);
	REQUIRE(offsets.size() == 64);

	int near_centre = 0;
	for (int i = 0; i < offsets.size(); i++) {
		CHECK(offsets[i].length() <= 1.0001f); // inside the unit sphere
		if (offsets[i].length() < 0.5f) {
			near_centre++;
		}
	}
	// Volume-uniform: only ~1/8 of samples lie within the inner half-radius
	// (which holds 1/8 of the volume). Certainly not the majority.
	CHECK(near_centre < offsets.size() / 2);
}

TEST_CASE("[Symphony][Spatial][Volumetric] Volume samples are deterministic") {
	Vector<Vector3> a, b;
	OcclusionSolver::generate_volume_samples(32, a);
	OcclusionSolver::generate_volume_samples(32, b);
	REQUIRE(a.size() == b.size());
	for (int i = 0; i < a.size(); i++) {
		CHECK(a[i].is_equal_approx(b[i]));
	}
}

// --- compute_volumetric: graduated occlusion ---------------------------

TEST_CASE("[Symphony][Spatial][Volumetric] Point source uses a single ray") {
	// radius below min_radius → point-source path, one ray.
	auto always_clear = [](const Vector3 &, const Vector3 &) { return true; };
	OcclusionSolver::VolumetricResult r = OcclusionSolver::compute_volumetric(
			Vector3(0, 0, 0), 0.0f, Vector3(10, 0, 0), 16, 0.05f, always_clear);
	CHECK(r.rays_issued == 1);
	CHECK(r.samples_taken == 1);
	CHECK(r.occlusion == doctest::Approx(0.0f));
}

TEST_CASE("[Symphony][Spatial][Volumetric] Open path reports zero occlusion") {
	auto always_clear = [](const Vector3 &, const Vector3 &) { return true; };
	OcclusionSolver::VolumetricResult r = OcclusionSolver::compute_volumetric(
			Vector3(0, 0, 0), 1.0f, Vector3(20, 0, 0), 24, 0.05f, always_clear);
	CHECK(r.occlusion == doctest::Approx(0.0f));
	CHECK(r.samples_taken > 0);
	CHECK(r.samples_visible == r.samples_taken);
}

TEST_CASE("[Symphony][Spatial][Volumetric] Fully blocked path reports full occlusion") {
	// Wall plane at x = 5 spanning all y,z. Any segment crossing x = 5 is
	// blocked. Source at origin (radius 1 → all samples x < 5, visible from
	// centre); listener at x = 20 → every sample→listener ray crosses the wall.
	const Vector3 source(0, 0, 0);
	const Vector3 listener(20, 0, 0);
	auto wall_at_x5 = [](const Vector3 &a, const Vector3 &b) {
		// Blocked if the segment straddles the x = 5 plane.
		return !((a.x < 5.0f && b.x > 5.0f) || (a.x > 5.0f && b.x < 5.0f));
	};
	OcclusionSolver::VolumetricResult r = OcclusionSolver::compute_volumetric(
			source, 1.0f, listener, 24, 0.05f, wall_at_x5);
	CHECK(r.occlusion == doctest::Approx(1.0f));
	CHECK(r.samples_taken > 0);
	CHECK(r.samples_visible == 0);
}

TEST_CASE("[Symphony][Spatial][Volumetric] Partial block is graduated (~half)") {
	// A listener-side occluder blocks samples in the lower half (y < 0) of the
	// source volume but not the upper half. All samples remain visible from the
	// source centre (no occluder near the source). Since ~half the volume
	// samples have y < 0, occlusion ≈ 0.5.
	const Vector3 source(0, 0, 0);
	const Vector3 listener(20, 0, 0);
	auto lower_half_blocked = [&](const Vector3 &a, const Vector3 &b) {
		// Centre→sample rays (b is a sample near the source) are always clear.
		if (b.distance_to(source) < 5.0f && a.is_equal_approx(source)) {
			return true;
		}
		// Sample→listener rays: blocked when the sample sits in the lower half.
		if (b.is_equal_approx(listener)) {
			return a.y >= 0.0f;
		}
		return true;
	};
	OcclusionSolver::VolumetricResult r = OcclusionSolver::compute_volumetric(
			source, 2.0f, listener, 128, 0.05f, lower_half_blocked);
	CHECK(r.samples_taken > 0);
	// Graduated, not binary — and close to half.
	CHECK(r.occlusion > 0.3f);
	CHECK(r.occlusion < 0.7f);
}

TEST_CASE("[Symphony][Spatial][Volumetric] Samples buried in geometry fall back to centre") {
	// Every centre→sample ray is blocked (source volume fully inside geometry),
	// so no sample is valid → fall back to the centre→listener test, which is
	// also blocked → occlusion 1 with a single reported sample.
	const Vector3 source(0, 0, 0);
	const Vector3 listener(100, 0, 0);
	auto all_blocked = [](const Vector3 &, const Vector3 &) { return false; };
	OcclusionSolver::VolumetricResult r = OcclusionSolver::compute_volumetric(
			source, 2.0f, listener, 16, 0.05f, all_blocked);
	CHECK(r.samples_taken == 1); // centre fallback
	CHECK(r.samples_visible == 0);
	CHECK(r.occlusion == doctest::Approx(1.0f));
}

TEST_CASE("[Symphony][Spatial][Volumetric] Ray count respects the sample budget") {
	auto always_clear = [](const Vector3 &, const Vector3 &) { return true; };
	const int samples = 12;
	OcclusionSolver::VolumetricResult r = OcclusionSolver::compute_volumetric(
			Vector3(0, 0, 0), 1.0f, Vector3(20, 0, 0), samples, 0.05f, always_clear);
	// At most 2 rays per sample (centre-visibility + listener-visibility).
	CHECK(r.rays_issued <= samples * 2);
}

TEST_CASE("[Symphony][Spatial][Volumetric] Null physics space is fully audible") {
	OcclusionSolver::VolumetricConfig cfg;
	OcclusionSolver::VolumetricResult r = OcclusionSolver::solve_volumetric(
			nullptr, Vector3(0, 0, 0), 1.0f, Vector3(10, 0, 0), Vector<RID>(), cfg);
	CHECK(r.occlusion == doctest::Approx(0.0f));
}

// --- Air absorption cutoff wiring --------------------------------------

TEST_CASE("[Symphony][Spatial][Volumetric] Air cutoff falls with distance") {
	// Phase 4.1: distance-absolute ISO 9613-1 fit; 2nd arg is now the artistic
	// scale (1.0 = physical), not max_distance. Cutoff still drops with distance.
	float near_hz = SpatialGraphWrapper::distance_to_air_cutoff(1.0f, 1.0f);
	float mid_hz = SpatialGraphWrapper::distance_to_air_cutoff(50.0f, 1.0f);
	float far_hz = SpatialGraphWrapper::distance_to_air_cutoff(300.0f, 1.0f);
	CHECK(near_hz > mid_hz);
	CHECK(mid_hz > far_hz);
	CHECK(far_hz >= 200.0f); // clamped floor
	CHECK(near_hz <= 20000.0f);
	// scale=0 disables the effect (wide open).
	CHECK(SpatialGraphWrapper::distance_to_air_cutoff(100.0f, 0.0f) == doctest::Approx(20000.0f));
	// Sanity: ~30 m at physical scale lands in the several-kHz range.
	float at30 = SpatialGraphWrapper::distance_to_air_cutoff(30.0f, 1.0f);
	CHECK(at30 > 4000.0f);
	CHECK(at30 < 12000.0f);
}

} // namespace TestSymphonyVolumetric
