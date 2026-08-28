/**************************************************************************/
/*  test_symphony_probe_scheduler.cpp                                     */
/*  Suite: [Symphony][Spatial][Scheduler] — unified ray budget (5.1).     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_probe_scheduler)

#include "modules/symphony/spatial/probe_scheduler.h"

namespace TestSymphonyProbeScheduler {

// The scheduler is pure logic (no raycasts) — drive schedule() directly with a
// synthetic EmitterInfo array and inspect the returned indices + metrics.

static ProbeScheduler::EmitterInfo make_info(int idx, float dist, int cost, bool audible = true, float importance = 1.0f) {
	ProbeScheduler::EmitterInfo info;
	info.emitter_index = idx;
	info.distance_sq = dist * dist;
	info.importance = importance;
	info.audible = audible;
	info.last_update_time = 100.0f; // always "due" (far above any interval)
	info.estimated_cost = cost;
	return info;
}

// --- Budget ceiling -----------------------------------------------------

TEST_CASE("[Symphony][Spatial][Scheduler] REGRESSION: billed rays stay within budget at 200 emitters") {
	ProbeScheduler scheduler;
	ProbeScheduler::Config cfg;
	cfg.ray_budget_per_frame = 64;
	cfg.base_rate_hz = 10.0f;
	scheduler.set_config(cfg);

	// 200 emitters, each costing 8 rays — far more demand than the 64-ray pool,
	// and no single emitter exceeds the budget.
	static ProbeScheduler::EmitterInfo infos[200];
	for (int i = 0; i < 200; i++) {
		infos[i] = make_info(i, 1.0f + (float)i, 8);
	}

	Vector<int> to_update;
	scheduler.schedule(infos, 200, 1.0f, to_update);
	// The estimate billed this frame must not exceed the pool.
	CHECK(scheduler.get_metrics().rays_issued <= cfg.ray_budget_per_frame);
	CHECK(to_update.size() > 0); // some emitters serviced
	CHECK(to_update.size() <= 200);
}

TEST_CASE("[Symphony][Spatial][Scheduler] Overshoot correction shrinks next frame's pool") {
	ProbeScheduler scheduler;
	ProbeScheduler::Config cfg;
	cfg.ray_budget_per_frame = 64;
	cfg.min_room_probe_budget = 16;
	scheduler.set_config(cfg);

	static ProbeScheduler::EmitterInfo infos[50];
	for (int i = 0; i < 50; i++) {
		infos[i] = make_info(i, 1.0f + (float)i, 8);
	}
	Vector<int> to_update;
	scheduler.schedule(infos, 50, 1.0f, to_update);

	// Simulate solvers over-issuing badly last frame.
	scheduler.report_actual_rays(200);
	scheduler.schedule(infos, 50, 1.0f, to_update);
	// The correction clamps the pool down, but never below the room-probe floor.
	CHECK(scheduler.get_metrics().rays_issued >= cfg.min_room_probe_budget);
	CHECK(scheduler.get_metrics().rays_issued <= cfg.ray_budget_per_frame);
}

// --- Near serviced before far ------------------------------------------

TEST_CASE("[Symphony][Spatial][Scheduler] Near emitters are prioritized over far ones") {
	ProbeScheduler scheduler;
	ProbeScheduler::Config cfg;
	cfg.ray_budget_per_frame = 16; // only room for ~2 emitters at cost 8
	scheduler.set_config(cfg);

	ProbeScheduler::EmitterInfo infos[4];
	infos[0] = make_info(0, 100.0f, 8); // far
	infos[1] = make_info(1, 2.0f, 8);   // near
	infos[2] = make_info(2, 200.0f, 8); // farther
	infos[3] = make_info(3, 1.0f, 8);   // nearest

	Vector<int> to_update;
	scheduler.schedule(infos, 4, 1.0f, to_update);
	REQUIRE(to_update.size() >= 1);
	// The nearest emitters (indices 3 and 1) should be picked before the far ones.
	bool has_near = to_update.find(3) != -1 || to_update.find(1) != -1;
	CHECK(has_near);
	// The single farthest emitter (index 2) should NOT be serviced under a tight
	// budget when nearer ones are due.
	CHECK(to_update.find(2) == -1);
}

// --- Cache-hit emitters bill fewer rays --------------------------------

TEST_CASE("[Symphony][Spatial][Scheduler] Cheaper (cache-hit) emitters let more be serviced") {
	ProbeScheduler scheduler;
	ProbeScheduler::Config cfg;
	cfg.ray_budget_per_frame = 32;
	scheduler.set_config(cfg);

	// All cost 2 (as if occlusion-only / room cache hit → 0 room rays).
	static ProbeScheduler::EmitterInfo cheap[40];
	for (int i = 0; i < 40; i++) {
		cheap[i] = make_info(i, 1.0f + (float)i, 2);
	}
	Vector<int> cheap_upd;
	scheduler.schedule(cheap, 40, 1.0f, cheap_upd);
	int serviced_cheap = cheap_upd.size();

	// All cost 8 (room cache miss → full fan). Fresh scheduler for a fair compare.
	ProbeScheduler scheduler2;
	scheduler2.set_config(cfg);
	static ProbeScheduler::EmitterInfo pricey[40];
	for (int i = 0; i < 40; i++) {
		pricey[i] = make_info(i, 1.0f + (float)i, 8);
	}
	Vector<int> pricey_upd;
	scheduler2.schedule(pricey, 40, 1.0f, pricey_upd);
	int serviced_pricey = pricey_upd.size();

	CHECK(serviced_cheap > serviced_pricey);
}

// --- Inaudible emitters skipped ----------------------------------------

TEST_CASE("[Symphony][Spatial][Scheduler] Inaudible emitters are skipped entirely") {
	ProbeScheduler scheduler;
	ProbeScheduler::Config cfg;
	cfg.ray_budget_per_frame = 64;
	scheduler.set_config(cfg);

	ProbeScheduler::EmitterInfo infos[3];
	infos[0] = make_info(0, 1.0f, 8, /*audible=*/false);
	infos[1] = make_info(1, 2.0f, 8, /*audible=*/true);
	infos[2] = make_info(2, 3.0f, 8, /*audible=*/false);

	Vector<int> to_update;
	scheduler.schedule(infos, 3, 1.0f, to_update);
	CHECK(to_update.find(0) == -1);
	CHECK(to_update.find(2) == -1);
	CHECK(to_update.find(1) != -1);
	CHECK(scheduler.get_metrics().emitters_skipped >= 2);
}

// --- Base rate throttle -------------------------------------------------

TEST_CASE("[Symphony][Spatial][Scheduler] Emitter not yet due is not scheduled") {
	ProbeScheduler scheduler;
	ProbeScheduler::Config cfg;
	cfg.ray_budget_per_frame = 64;
	cfg.base_rate_hz = 10.0f; // base interval 0.1 s
	scheduler.set_config(cfg);

	ProbeScheduler::EmitterInfo info = make_info(0, 1.0f, 8);
	info.last_update_time = 0.001f; // just updated — well under 0.1 s

	Vector<int> to_update;
	scheduler.schedule(&info, 1, 0.016f, to_update);
	CHECK(to_update.is_empty());
}

// --- Fairness: round-robin over frames ---------------------------------

TEST_CASE("[Symphony][Spatial][Scheduler] Round-robin eventually services all due emitters") {
	ProbeScheduler scheduler;
	ProbeScheduler::Config cfg;
	cfg.ray_budget_per_frame = 16; // ~2 emitters/frame at cost 8
	scheduler.set_config(cfg);

	const int N = 8;
	static ProbeScheduler::EmitterInfo infos[N];

	bool seen[N] = { false };
	// Run several frames; every due emitter should get serviced at least once as
	// the priority-order cursor walks the list.
	for (int frame = 0; frame < 12; frame++) {
		for (int i = 0; i < N; i++) {
			// Equal distance so priority ties → the round-robin cursor decides.
			infos[i] = make_info(i, 5.0f, 8);
		}
		Vector<int> to_update;
		scheduler.schedule(infos, N, 1.0f, to_update);
		for (int k = 0; k < to_update.size(); k++) {
			seen[to_update[k]] = true;
		}
	}
	for (int i = 0; i < N; i++) {
		CHECK(seen[i]);
	}
}

} // namespace TestSymphonyProbeScheduler
