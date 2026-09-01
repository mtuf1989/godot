/**************************************************************************/
/*  test_symphony_probe_cache.cpp                                         */
/*  Suite: [Symphony][Spatial][ProbeCache] — spatial-hash room cache.     */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_probe_cache)

#include "modules/symphony/spatial/probe_cache.h"

namespace TestSymphonyProbeCache {

static ProbeCache::RoomProbeResult make_result(float rt60) {
	ProbeCache::RoomProbeResult r;
	r.rt60 = rt60;
	r.volume = 100.0f;
	r.reverb_send = 0.5f;
	return r;
}

// --- Hit / miss ---------------------------------------------------------

TEST_CASE("[Symphony][Spatial][ProbeCache] Store then lookup in the same cell hits") {
	ProbeCache cache;
	cache.store(Vector3(1, 1, 1), make_result(1.5f));

	ProbeCache::RoomProbeResult out;
	CHECK(cache.lookup(Vector3(1.2f, 1.2f, 1.2f), out)); // same 3 m cell
	CHECK(out.rt60 == doctest::Approx(1.5f));
	CHECK(cache.get_metrics().hits == 1);
}

TEST_CASE("[Symphony][Spatial][ProbeCache] Lookup in an empty cell misses") {
	ProbeCache cache;
	ProbeCache::RoomProbeResult out;
	CHECK_FALSE(cache.lookup(Vector3(50, 50, 50), out));
	CHECK(cache.get_metrics().misses == 1);
}

TEST_CASE("[Symphony][Spatial][ProbeCache] Distant positions land in different cells") {
	ProbeCache cache;
	cache.store(Vector3(0, 0, 0), make_result(1.0f));
	ProbeCache::RoomProbeResult out;
	// 3 m default cell; 10 m away is a different cell → miss.
	CHECK_FALSE(cache.lookup(Vector3(10, 0, 0), out));
}

// --- Staleness ----------------------------------------------------------

TEST_CASE("[Symphony][Spatial][ProbeCache] Entries go stale past max_age") {
	ProbeCache cache;
	ProbeCache::Config cfg;
	cfg.max_age_seconds = 0.5f;
	cache.set_config(cfg);

	cache.store(Vector3(0, 0, 0), make_result(2.0f));
	ProbeCache::RoomProbeResult out;
	CHECK(cache.lookup(Vector3(0, 0, 0), out)); // fresh

	cache.advance_time(0.6f); // now older than max_age
	CHECK_FALSE(cache.lookup(Vector3(0, 0, 0), out)); // stale → miss
}

// --- would_hit is side-effect free -------------------------------------

TEST_CASE("[Symphony][Spatial][ProbeCache] would_hit predicts without touching metrics") {
	ProbeCache cache;
	cache.store(Vector3(2, 0, 0), make_result(1.0f));
	cache.reset_metrics();

	CHECK(cache.would_hit(Vector3(2, 0, 0)));
	CHECK_FALSE(cache.would_hit(Vector3(50, 0, 0)));
	// No metric mutation from would_hit.
	CHECK(cache.get_metrics().hits == 0);
	CHECK(cache.get_metrics().misses == 0);
}

TEST_CASE("[Symphony][Spatial][ProbeCache] would_hit respects staleness") {
	ProbeCache cache;
	ProbeCache::Config cfg;
	cfg.max_age_seconds = 0.5f;
	cache.set_config(cfg);
	cache.store(Vector3(0, 0, 0), make_result(1.0f));
	CHECK(cache.would_hit(Vector3(0, 0, 0)));
	cache.advance_time(1.0f);
	CHECK_FALSE(cache.would_hit(Vector3(0, 0, 0)));
}

// --- Invalidation -------------------------------------------------------

TEST_CASE("[Symphony][Spatial][ProbeCache] invalidate_all clears and counts evictions") {
	ProbeCache cache;
	cache.store(Vector3(0, 0, 0), make_result(1.0f));
	cache.store(Vector3(10, 0, 0), make_result(1.0f));
	cache.store(Vector3(20, 0, 0), make_result(1.0f));
	cache.reset_metrics();

	cache.invalidate_all();
	// Phase 5.5: evictions counts the size BEFORE clearing (not zero).
	CHECK(cache.get_metrics().evictions == 3);
	CHECK(cache.get_metrics().total_entries == 0);

	ProbeCache::RoomProbeResult out;
	CHECK_FALSE(cache.lookup(Vector3(0, 0, 0), out));
}

TEST_CASE("[Symphony][Spatial][ProbeCache] invalidate_near drops only nearby cells") {
	ProbeCache cache;
	cache.store(Vector3(0, 0, 0), make_result(1.0f));
	cache.store(Vector3(30, 0, 0), make_result(2.0f)); // far away

	cache.invalidate_near(Vector3(0, 0, 0), 3.0f);

	ProbeCache::RoomProbeResult out;
	CHECK_FALSE(cache.lookup(Vector3(0, 0, 0), out)); // dropped
	CHECK(cache.lookup(Vector3(30, 0, 0), out));      // untouched
}

// --- LRU eviction -------------------------------------------------------

TEST_CASE("[Symphony][Spatial][ProbeCache] Exceeding max_entries evicts the oldest") {
	ProbeCache cache;
	ProbeCache::Config cfg;
	cfg.max_entries = 3;
	cfg.cell_size = 1.0f;
	cfg.max_age_seconds = 1000.0f; // keep everything fresh so age doesn't interfere
	cache.set_config(cfg);

	// Store 4 entries in 4 distinct cells; advancing time between stores so each
	// has a strictly increasing timestamp (the oldest is the first stored).
	cache.store(Vector3(0, 0, 0), make_result(1.0f));
	cache.advance_time(0.01f);
	cache.store(Vector3(5, 0, 0), make_result(2.0f));
	cache.advance_time(0.01f);
	cache.store(Vector3(10, 0, 0), make_result(3.0f));
	cache.advance_time(0.01f);
	cache.store(Vector3(15, 0, 0), make_result(4.0f)); // triggers eviction of the oldest

	ProbeCache::RoomProbeResult out;
	CHECK_FALSE(cache.lookup(Vector3(0, 0, 0), out)); // oldest evicted
	CHECK(cache.lookup(Vector3(15, 0, 0), out));      // newest present
	CHECK(cache.get_metrics().total_entries <= 3);
}

} // namespace TestSymphonyProbeCache
