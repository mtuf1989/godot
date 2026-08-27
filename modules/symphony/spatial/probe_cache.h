#ifndef PROBE_CACHE_H
#define PROBE_CACHE_H

#include "core/math/vector3.h"
#include "core/templates/hash_map.h"

// Spatial-hash probe cache for room acoustics results.
//
// Emitters in the same spatial cell share room-probe results (RT60, volume,
// reverb send) instead of each firing an independent Fibonacci ray fan.
// This dramatically reduces ray count for clustered emitters (e.g., multiple
// footstep sources in the same room).
//
// Cache entries are time-invalidated: if an entry is older than max_age_seconds,
// it's considered stale and a new probe is required.
//
// Usage flow (Task 9 will produce the data):
// 1. Emitter needs room data → compute cell from position
// 2. Check cache for valid entry in that cell
// 3. If hit: reuse RT60/volume/absorption data
// 4. If miss: mark for probe, store result when available
class ProbeCache {
public:
	struct RoomProbeResult {
		float rt60 = 0.0f;           // Sabine RT60 estimate
		float volume = 0.0f;         // Estimated room volume (m³)
		float mean_absorption = 0.0f; // Mean absorption coefficient
		float openness = 0.0f;       // Ratio of escaped rays (0=sealed, 1=open sky)
		float reverb_send = 0.0f;    // Computed reverb send level
		float timestamp = 0.0f;      // Time when this result was computed
	};

	struct Config {
		float cell_size = 3.0f;        // Spatial hash cell size in meters
		float max_age_seconds = 0.5f;  // Cache entries older than this are stale
		int max_entries = 512;         // Maximum cached cells (LRU eviction beyond this)
	};

	struct Metrics {
		int hits = 0;
		int misses = 0;
		int evictions = 0;
		int total_entries = 0;
	};

private:
	Config config;
	Metrics last_metrics;
	float current_time = 0.0f;

	// Spatial hash: cell key → cached result
	// Cell key is computed from (x/cell_size, y/cell_size, z/cell_size) quantized to int.
	struct CellKey {
		int32_t x, y, z;
		bool operator==(const CellKey &p_other) const {
			return x == p_other.x && y == p_other.y && z == p_other.z;
		}
	};

	struct CellKeyHasher {
		static uint32_t hash(const CellKey &p_key) {
			// FNV-1a style hash for 3D cell coordinates.
			uint32_t h = 2166136261u;
			h ^= (uint32_t)p_key.x; h *= 16777619u;
			h ^= (uint32_t)p_key.y; h *= 16777619u;
			h ^= (uint32_t)p_key.z; h *= 16777619u;
			return h;
		}
	};

	HashMap<CellKey, RoomProbeResult, CellKeyHasher> cache;

	CellKey _position_to_cell(const Vector3 &p_pos) const;
	void _evict_oldest();

public:
	// Advance time (call once per frame).
	void advance_time(float p_delta) { current_time += p_delta; }

	// Lookup a cached room probe result for the given position.
	// Returns true if a valid (non-stale) entry exists.
	bool lookup(const Vector3 &p_position, RoomProbeResult &r_result) const;

	// Store a room probe result at the given position.
	void store(const Vector3 &p_position, const RoomProbeResult &p_result);

	// Invalidate all entries (e.g., on geometry change).
	void invalidate_all();

	// Invalidate entries near a position (localized geometry change).
	void invalidate_near(const Vector3 &p_position, float p_radius);

	// Configuration
	void set_config(const Config &p_config) { config = p_config; }
	const Config &get_config() const { return config; }

	void set_cell_size(float p_size) { config.cell_size = MAX(p_size, 0.1f); }
	float get_cell_size() const { return config.cell_size; }

	// Metrics
	const Metrics &get_metrics() const { return last_metrics; }
	void reset_metrics() { last_metrics = Metrics(); last_metrics.total_entries = cache.size(); }

	ProbeCache();
};

#endif // PROBE_CACHE_H
