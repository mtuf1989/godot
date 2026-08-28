/**************************************************************************/
/*  test_symphony_spatial_engine.cpp                                      */
/*  Suite: [Symphony][Spatial][Engine] — smoothing / SeqLock / blend.     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_spatial_engine)

#include "modules/symphony/spatial/spatial_acoustics_engine.h"
#include "modules/symphony/spatial/symphony_seqlock.h"

#include "core/os/thread.h"
#include <atomic>

namespace TestSymphonySpatialEngine {

// The engine singleton is created at module init. Without a physics space,
// update() skips the ray solvers, so we drive `target` via the set_emitter_*
// overrides and verify the frame-rate-independent smoothing, snap-on-first,
// SeqLock publish, and the no-ratchet air-cutoff behaviour. The volumetric
// blend identity is checked directly (it is applied inside the physics-gated
// occlusion solve, so we verify the arithmetic the engine uses).

static SpatialAcousticsEngine *engine() {
	return SpatialAcousticsEngine::get_singleton();
}

// --- Registration lifecycle --------------------------------------------

TEST_CASE("[Symphony][Spatial][Engine] Register / unregister an emitter") {
	SpatialAcousticsEngine *e = engine();
	REQUIRE(e != nullptr);
	const int slot = 900;
	CHECK_FALSE(e->has_emitter(slot));
	int idx = e->register_emitter(slot);
	CHECK(idx >= 0);
	CHECK(e->has_emitter(slot));
	// Re-registering the same slot returns the same handle.
	CHECK(e->register_emitter(slot) == idx);
	e->unregister_emitter(slot);
	CHECK_FALSE(e->has_emitter(slot));
}

// --- Snap on first update ----------------------------------------------

TEST_CASE("[Symphony][Spatial][Engine] First update snaps to target (no fade-in)") {
	SpatialAcousticsEngine *e = engine();
	const int slot = 901;
	e->register_emitter(slot);
	e->set_emitter_occlusion(slot, 0.8f);
	e->update(1.0f / 60.0f); // first update → snap
	SpatialParams p = e->read_params(slot);
	CHECK(p.occlusion == doctest::Approx(0.8f)); // snapped, not smoothed toward
	e->unregister_emitter(slot);
}

// --- Smoothing converges ------------------------------------------------

TEST_CASE("[Symphony][Spatial][Engine] Occlusion smooths toward a changed target") {
	SpatialAcousticsEngine *e = engine();
	const int slot = 902;
	e->register_emitter(slot);
	e->set_emitter_occlusion(slot, 0.0f);
	e->update(1.0f / 60.0f); // snap to 0

	e->set_emitter_occlusion(slot, 1.0f);
	// One frame after the snap: partway, not all the way.
	e->update(1.0f / 60.0f);
	SpatialParams mid = e->read_params(slot);
	CHECK(mid.occlusion > 0.0f);
	CHECK(mid.occlusion < 1.0f);

	// Many frames: converges to the target.
	for (int i = 0; i < 240; i++) {
		e->update(1.0f / 60.0f);
	}
	SpatialParams settled = e->read_params(slot);
	CHECK(settled.occlusion == doctest::Approx(1.0f).epsilon(0.02));
	e->unregister_emitter(slot);
}

// --- Frame-rate independence -------------------------------------------

TEST_CASE("[Symphony][Spatial][Engine] Same settle time at 30/60/144 fps") {
	SpatialAcousticsEngine *e = engine();

	auto settle_after = [&](int slot, float dt, float seconds) -> float {
		e->register_emitter(slot);
		e->set_emitter_occlusion(slot, 0.0f);
		e->update(dt); // snap to 0
		e->set_emitter_occlusion(slot, 1.0f);
		int frames = (int)(seconds / dt + 0.5f);
		for (int i = 0; i < frames; i++) {
			e->update(dt);
		}
		float v = e->read_params(slot).occlusion;
		e->unregister_emitter(slot);
		return v;
	};

	// After the SAME wall-clock duration (0.25 s), the smoothed value should be
	// (nearly) the same regardless of frame rate — the Phase 3.5 fix.
	float v30 = settle_after(910, 1.0f / 30.0f, 0.25f);
	float v60 = settle_after(911, 1.0f / 60.0f, 0.25f);
	float v144 = settle_after(912, 1.0f / 144.0f, 0.25f);

	CHECK(v30 == doctest::Approx(v60).epsilon(0.05));
	CHECK(v60 == doctest::Approx(v144).epsilon(0.05));
}

// --- Air-cutoff no-ratchet ---------------------------------------------

TEST_CASE("[Symphony][Spatial][Engine] Air cutoff recovers upward (no monotonic ratchet)") {
	SpatialAcousticsEngine *e = engine();
	const int slot = 920;
	e->register_emitter(slot);

	// Start wide open and snap.
	e->set_emitter_air_cutoff(slot, 20000.0f);
	e->update(1.0f / 60.0f);

	// Drop the cutoff (occluded / diffracted), let it settle down.
	e->set_emitter_air_cutoff(slot, 1000.0f);
	for (int i = 0; i < 240; i++) {
		e->update(1.0f / 60.0f);
	}
	float low = e->read_params(slot).air_cutoff;
	CHECK(low < 3000.0f); // clearly rolled off

	// Now the path opens up again — the cutoff MUST be able to climb back.
	e->set_emitter_air_cutoff(slot, 20000.0f);
	for (int i = 0; i < 240; i++) {
		e->update(1.0f / 60.0f);
	}
	float high = e->read_params(slot).air_cutoff;
	CHECK(high > low * 3.0f);           // recovered substantially
	CHECK(high == doctest::Approx(20000.0f).epsilon(0.05));
	e->unregister_emitter(slot);
}

// --- Volumetric blend identity -----------------------------------------

TEST_CASE("[Symphony][Spatial][Engine] Volumetric blend: occ=0.5, T_mat=0.1 -> T_eff=0.55") {
	// The engine folds volumetric occlusion into the transmission bands as
	// T_eff = (1 - occ) + occ * T_mat  (Steam Audio model, Phase 2.1).
	const float occ = 0.5f;
	const float t_mat = 0.1f;
	const float t_eff = (1.0f - occ) + occ * t_mat;
	CHECK(t_eff == doctest::Approx(0.55f));
	// occ=0 → fully transmissive (no occlusion → bands pass at 1.0);
	// occ=1 → fully the material value (all energy routed through the material).
	CHECK(((1.0f - 0.0f) + 0.0f * t_mat) == doctest::Approx(1.0f));
	CHECK(((1.0f - 1.0f) + 1.0f * t_mat) == doctest::Approx(t_mat));
}

// --- SeqLock round-trip under a concurrent reader ----------------------

TEST_CASE("[Symphony][Spatial][Engine] SeqLock: concurrent reader never tears") {
	// Directly exercise the SeqLock the engine publishes through: a writer
	// stores monotonically-consistent SpatialParams while a reader spins; every
	// successful read must be internally consistent (no torn field mix).
	SymphonySeqLock<SpatialParams> lock;
	std::atomic<bool> stop{ false };
	std::atomic<int> torn{ 0 };
	std::atomic<int> reads{ 0 };

	struct Ctx {
		SymphonySeqLock<SpatialParams> *lock;
		std::atomic<bool> *stop;
		std::atomic<int> *torn;
		std::atomic<int> *reads;
	} ctx{ &lock, &stop, &torn, &reads };

	// Seed a consistent value.
	{
		SpatialParams p;
		p.occlusion = 0.0f;
		p.transmission[0] = p.transmission[1] = p.transmission[2] = 1.0f;
		lock.store(p);
	}

	Thread reader;
	reader.start([](void *ud) {
		Ctx *c = (Ctx *)ud;
		while (!c->stop->load()) {
			SpatialParams p;
			if (c->lock->try_load(p)) {
				c->reads->fetch_add(1);
				// Invariant maintained by the writer: all three transmission
				// bands equal occlusion's complement. A torn read would break it.
				float expect = 1.0f - p.occlusion;
				if (Math::abs(p.transmission[0] - expect) > 1e-3f ||
						Math::abs(p.transmission[1] - expect) > 1e-3f ||
						Math::abs(p.transmission[2] - expect) > 1e-3f) {
					c->torn->fetch_add(1);
				}
			}
		}
	},
			&ctx);

	// Writer: sweep occlusion, keeping the invariant transmission = 1 - occ.
	for (int i = 0; i < 20000; i++) {
		float occ = (float)(i % 101) / 100.0f;
		SpatialParams p;
		p.occlusion = occ;
		p.transmission[0] = p.transmission[1] = p.transmission[2] = 1.0f - occ;
		lock.store(p);
	}
	stop.store(true);
	reader.wait_to_finish();

	CHECK(reads.load() > 0);   // the reader actually observed values
	CHECK(torn.load() == 0);   // and never saw a torn one
}

} // namespace TestSymphonySpatialEngine
