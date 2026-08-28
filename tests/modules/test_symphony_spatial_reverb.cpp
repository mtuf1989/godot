/**************************************************************************/
/*  test_symphony_spatial_reverb.cpp                                      */
/*  Suite: [Symphony][Spatial][Reverb] — shared reverb pool (Task 10).    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_spatial_reverb)

#include "modules/symphony/spatial/reverb_pool.h"

namespace TestSymphonySpatialReverb {

static ReverbPool make_pool(int p_slots, float p_cluster = 0.4f, float p_crossfade = 0.25f) {
	ReverbPool pool;
	ReverbPool::Config cfg;
	cfg.active_slots = p_slots;
	cfg.rt60_cluster_threshold = p_cluster;
	cfg.crossfade_seconds = p_crossfade;
	pool.init(cfg);
	return pool;
}

TEST_CASE("[Symphony][Spatial][Reverb] Init clamps slot count into [1, MAX_SLOTS]") {
	ReverbPool pool = make_pool(99);
	CHECK(pool.active_slot_count() == ReverbPool::MAX_SLOTS);

	ReverbPool pool_zero = make_pool(0);
	CHECK(pool_zero.active_slot_count() == 1);
}

TEST_CASE("[Symphony][Spatial][Reverb] Anti-regression: pool never touches AudioServer (R4)") {
	ReverbPool pool = make_pool(8);
	CHECK(pool.touched_audio_server() == false);

	// Exercise the whole lifecycle; the flag must stay false throughout.
	pool.assign(0, 1.5f, 0.5f, 0.8f);
	pool.assign(1, 3.0f, 0.4f, 0.6f);
	pool.update(0.016f);
	pool.assign(0, 2.9f, 0.4f, 0.6f); // Trigger a migration.
	for (int i = 0; i < 30; i++) {
		pool.update(0.016f);
	}
	pool.release(0);
	pool.release(1);
	pool.update(0.016f);

	CHECK(pool.touched_audio_server() == false);
}

TEST_CASE("[Symphony][Spatial][Reverb] Emitters with similar RT60 share one slot") {
	ReverbPool pool = make_pool(8, /*cluster=*/0.4f);

	// Three emitters all around RT60 ~1.5s → one cluster.
	int s0 = pool.assign(10, 1.50f, 0.5f, 0.7f);
	int s1 = pool.assign(11, 1.55f, 0.5f, 0.7f);
	int s2 = pool.assign(12, 1.60f, 0.5f, 0.7f);
	pool.update(0.016f);

	CHECK(s0 == s1);
	CHECK(s1 == s2);
	CHECK(pool.get_metrics().active_slot_count == 1);
	CHECK(pool.get_metrics().assigned_emitters == 3);
}

TEST_CASE("[Symphony][Spatial][Reverb] Distinct RT60 clusters take distinct slots") {
	ReverbPool pool = make_pool(8, /*cluster=*/0.4f);

	int a = pool.assign(0, 0.3f, 0.5f, 0.9f); // small dead room
	int b = pool.assign(1, 2.5f, 0.4f, 0.7f); // medium hall
	int c = pool.assign(2, 6.0f, 0.2f, 0.5f); // cathedral
	pool.update(0.016f);

	CHECK(a != b);
	CHECK(b != c);
	CHECK(a != c);
	CHECK(pool.get_metrics().active_slot_count == 3);
}

TEST_CASE("[Symphony][Spatial][Reverb] Slot params derive from RT60 and damping") {
	ReverbPool pool = make_pool(8);

	pool.assign(0, 3.0f, 0.7f, 0.8f);
	pool.update(0.016f);

	int slot = -1;
	float send = 0.0f;
	REQUIRE(pool.emitter_slot(0, slot, send));

	const ReverbPool::SlotParams &sp = pool.slot_params(slot);
	CHECK(sp.decay_time == doctest::Approx(3.0f).epsilon(0.01f));
	CHECK(sp.damping == doctest::Approx(0.7f).epsilon(0.01f));
	// room_size is a monotonic map of RT60/max_rt60 (default max 10s).
	CHECK(sp.room_size == doctest::Approx(0.3f).epsilon(0.05f));
	// wet_gain RAMPS in over crossfade_seconds (Phase 3.6), so after one small
	// step it is partway up, not snapped to 1. Drive enough frames to complete
	// the ramp (default crossfade 0.25s), then it settles at 1.0.
	CHECK(sp.wet_gain > 0.0f);
	CHECK(sp.wet_gain < 1.0f);
	for (int i = 0; i < 20; i++) {
		pool.assign(0, 3.0f, 0.7f, 0.8f);
		pool.update(0.016f);
	}
	CHECK(sp.wet_gain == doctest::Approx(1.0f));
}

TEST_CASE("[Symphony][Spatial][Reverb] Slot params are a contiguous block (GPU offload)") {
	ReverbPool pool = make_pool(4);
	const ReverbPool::SlotParams *block = pool.slot_param_block();
	REQUIRE(block != nullptr);

	// The block is a plain contiguous array; verify stride equals struct size.
	const ReverbPool::SlotParams *s0 = &pool.slot_params(0);
	const ReverbPool::SlotParams *s1 = &pool.slot_params(1);
	CHECK(block == s0);
	CHECK((s1 - s0) == 1);
	CHECK(sizeof(ReverbPool::SlotParams) == 5 * sizeof(float));
}

TEST_CASE("[Symphony][Spatial][Reverb] First assignment snaps (no crossfade)") {
	ReverbPool pool = make_pool(8);
	pool.assign(0, 2.0f, 0.5f, 0.6f);
	CHECK(pool.is_migrating(0) == false);

	int slot = -1;
	float send = 0.0f;
	REQUIRE(pool.emitter_slot(0, slot, send));
	CHECK(send == doctest::Approx(0.6f)); // Full send immediately (crossfade == 1).
}

TEST_CASE("[Symphony][Spatial][Reverb] Migration crossfades smoothly then completes") {
	// Two well-separated clusters so a change of RT60 forces a slot change.
	ReverbPool pool = make_pool(8, /*cluster=*/0.4f, /*crossfade=*/0.25f);

	int slot_a = pool.assign(0, 0.5f, 0.5f, 1.0f);
	pool.update(0.016f);

	// Seed a second cluster far away, then migrate emitter 0 into it.
	pool.assign(1, 5.0f, 0.3f, 1.0f);
	pool.update(0.016f);
	int slot_b = pool.assign(0, 5.0f, 0.3f, 1.0f);
	CHECK(slot_b != slot_a);
	REQUIRE(pool.is_migrating(0));

	// During migration, the emitter sends to BOTH slots; the sum of sends is
	// energy-preserving-ish and neither jumps to zero (no click).
	float prev_send_first = 0.0f;
	int prev_slot = -1;
	REQUIRE(pool.emitter_prev_slot(0, prev_slot, prev_send_first));
	CHECK(prev_slot == slot_a);

	float last_new_send = -1.0f;
	float last_prev_send = 2.0f;
	int steps = 0;
	while (pool.is_migrating(0) && steps < 100) {
		pool.update(0.016f);
		int cur_slot = -1, cur_prev = -1;
		float cur_send = 0.0f, cur_prev_send = 0.0f;
		REQUIRE(pool.emitter_slot(0, cur_slot, cur_send));
		CHECK(cur_slot == slot_b);
		// New-slot send is monotonically non-decreasing.
		CHECK(cur_send >= last_new_send - 0.0001f);
		last_new_send = cur_send;
		// Prev-slot send is monotonically non-increasing while migrating.
		if (pool.emitter_prev_slot(0, cur_prev, cur_prev_send)) {
			CHECK(cur_prev_send <= last_prev_send + 0.0001f);
			last_prev_send = cur_prev_send;
		}
		steps++;
	}

	CHECK(pool.is_migrating(0) == false);
	// After completion, full send to the new slot, none to any prev slot.
	int fs = -1;
	float fsend = 0.0f;
	REQUIRE(pool.emitter_slot(0, fs, fsend));
	CHECK(fs == slot_b);
	CHECK(fsend == doctest::Approx(1.0f).epsilon(0.01f));
	int dummy = -1;
	float dummy_send = 0.0f;
	CHECK(pool.emitter_prev_slot(0, dummy, dummy_send) == false);
}

TEST_CASE("[Symphony][Spatial][Reverb] Exceeding N clusters degrades to nearest match") {
	// Only 2 slots, but 3 clearly distinct RT60 clusters.
	ReverbPool pool = make_pool(2, /*cluster=*/0.4f);

	int a = pool.assign(0, 0.5f, 0.5f, 0.8f);
	int b = pool.assign(1, 5.0f, 0.3f, 0.8f);
	pool.update(0.016f);
	CHECK(a != b);

	// A third distinct cluster (RT60 ~2.5) is outside the 0.4s threshold of
	// both occupied slots and has no free slot; it must fall back to the
	// nearest existing cluster rather than allocate. |2.5-0.5|=2.0 vs
	// |2.5-5.0|=2.5 → nearest is slot a.
	int c = pool.assign(2, 2.5f, 0.4f, 0.8f);
	pool.update(0.016f);

	CHECK((c == a || c == b));
	CHECK(c == a); // Nearest by RT60.
	CHECK(pool.get_metrics().degraded_assignments >= 1);
	CHECK(pool.active_slot_count() == 2);
}

TEST_CASE("[Symphony][Spatial][Reverb] Release frees a slot for reuse") {
	ReverbPool pool = make_pool(2, /*cluster=*/0.4f);

	int a = pool.assign(0, 0.5f, 0.5f, 0.8f);
	int b = pool.assign(1, 5.0f, 0.3f, 0.8f);
	pool.update(0.016f);
	CHECK(a != b);
	CHECK(pool.get_metrics().active_slot_count == 2);

	// Free one cluster; its slot goes idle after the next update.
	pool.release(1);
	pool.update(0.016f);
	CHECK(pool.get_metrics().active_slot_count == 1);
	// wet_gain now RAMPS down (Phase 3.6); drive a few frames for the tail to
	// fade fully out before asserting it reached idle.
	for (int i = 0; i < 20; i++) {
		pool.update(0.016f);
	}
	CHECK(pool.slot_params(b).wet_gain == doctest::Approx(0.0f));

	// A new distinct cluster can now claim the freed slot.
	int c = pool.assign(2, 5.0f, 0.3f, 0.8f);
	pool.update(0.016f);
	CHECK(c == b);
	CHECK(pool.get_metrics().active_slot_count == 2);
}

TEST_CASE("[Symphony][Spatial][Reverb] Stable assignment does not spuriously migrate") {
	ReverbPool pool = make_pool(8, /*cluster=*/0.4f);
	pool.assign(0, 2.0f, 0.5f, 0.7f);
	pool.update(0.016f);

	pool.reset_migration_metrics();
	// Re-assign with the same descriptor repeatedly — no migrations expected.
	for (int i = 0; i < 10; i++) {
		pool.assign(0, 2.0f, 0.5f, 0.7f);
		pool.update(0.016f);
	}
	CHECK(pool.get_metrics().migrations == 0);
	CHECK(pool.is_migrating(0) == false);
}

TEST_CASE("[Symphony][Spatial][Reverb] Reset clears all state") {
	ReverbPool pool = make_pool(4);
	pool.assign(0, 2.0f, 0.5f, 0.7f);
	pool.assign(1, 5.0f, 0.3f, 0.6f);
	pool.update(0.016f);
	REQUIRE(pool.get_metrics().assigned_emitters == 2);

	pool.reset();
	pool.update(0.016f);
	CHECK(pool.get_metrics().assigned_emitters == 0);
	CHECK(pool.get_metrics().active_slot_count == 0);
	int slot = -1;
	float send = 0.0f;
	CHECK(pool.emitter_slot(0, slot, send) == false);
}

} // namespace TestSymphonySpatialReverb
