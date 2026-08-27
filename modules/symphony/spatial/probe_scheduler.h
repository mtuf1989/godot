#ifndef PROBE_SCHEDULER_H
#define PROBE_SCHEDULER_H

#include "core/math/vector3.h"
#include "core/templates/vector.h"

// Centralized probe scheduler that manages a per-frame ray budget and decides
// which emitters get updated each frame.
//
// Key properties:
// - Total rays per frame never exceeds the configured budget
// - Nearer emitters update more frequently than distant ones
// - Base rate is 10 Hz (configurable) — no emitter updates faster than this
// - Inaudible/virtual emitters are skipped entirely
// - Round-robin ensures fairness when budget is saturated
//
// The scheduler does NOT perform raycasts itself — it produces a list of
// emitter indices to process this frame.
class ProbeScheduler {
public:
	struct EmitterInfo {
		int emitter_index = -1;
		float distance_sq = 0.0f;  // Distance² to listener (for priority sorting)
		float importance = 0.0f;    // From VoicePool importance (priority × distance factor)
		bool audible = true;        // false = virtualized or beyond max distance
		float last_update_time = 0.0f; // Time since last occlusion update (seconds)
	};

	struct Config {
		int ray_budget_per_frame = 64;   // Max rays issued per frame across all emitters
		int rays_per_emitter = 8;        // Rays used per occlusion solve (matches OcclusionSolver::Config::max_hits)
		float base_rate_hz = 10.0f;      // Base update rate — no emitter updates faster than this
		float near_distance = 5.0f;      // Emitters closer than this always get priority
		float far_multiplier = 3.0f;     // Distant emitters can wait this many times longer
	};

	struct Metrics {
		int rays_issued = 0;        // Rays actually issued this frame
		int emitters_serviced = 0;  // Emitters that got an occlusion update this frame
		int emitters_skipped = 0;   // Emitters skipped (inaudible/over budget)
		int total_active = 0;       // Total active emitters
	};

private:
	Config config;
	Metrics last_metrics;

	// Round-robin cursor to ensure fairness when budget is exhausted.
	int round_robin_cursor = 0;

	// Scratch buffer for sorted candidates (avoids per-frame allocation).
	Vector<int> candidates; // Indices into EmitterInfo array, sorted by priority.

public:
	// Schedule which emitters should be updated this frame.
	// p_emitters: array of emitter info (caller fills from engine state)
	// p_count: number of entries in the array
	// p_delta: frame delta time
	// r_to_update: output — indices of emitters that should run occlusion this frame
	// Returns the number of emitters scheduled.
	int schedule(const EmitterInfo *p_emitters, int p_count, float p_delta, Vector<int> &r_to_update);

	// Configuration
	void set_config(const Config &p_config) { config = p_config; }
	const Config &get_config() const { return config; }

	void set_ray_budget(int p_budget) { config.ray_budget_per_frame = MAX(p_budget, 1); }
	int get_ray_budget() const { return config.ray_budget_per_frame; }

	void set_base_rate_hz(float p_rate) { config.base_rate_hz = CLAMP(p_rate, 1.0f, 60.0f); }
	float get_base_rate_hz() const { return config.base_rate_hz; }

	// Metrics from last schedule() call
	const Metrics &get_metrics() const { return last_metrics; }

	ProbeScheduler();
};

#endif // PROBE_SCHEDULER_H
