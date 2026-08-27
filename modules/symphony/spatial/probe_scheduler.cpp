#include "probe_scheduler.h"
#include "core/math/math_funcs.h"

ProbeScheduler::ProbeScheduler() {
}

int ProbeScheduler::schedule(const EmitterInfo *p_emitters, int p_count, float p_delta, Vector<int> &r_to_update) {
	r_to_update.clear();
	last_metrics = Metrics();

	if (p_count <= 0 || p_delta <= 0.0f) {
		return 0;
	}

	// How many emitters can we service this frame given the ray budget?
	int max_emitters_this_frame = config.ray_budget_per_frame / MAX(config.rays_per_emitter, 1);
	if (max_emitters_this_frame <= 0) {
		max_emitters_this_frame = 1;
	}

	// Base update interval (seconds). No emitter updates faster than this.
	float base_interval = 1.0f / config.base_rate_hz;

	// Build candidate list: emitters that are due for an update.
	candidates.clear();
	int total_active = 0;

	for (int i = 0; i < p_count; i++) {
		const EmitterInfo &info = p_emitters[i];
		if (info.emitter_index < 0) {
			continue;
		}
		total_active++;

		// Skip inaudible emitters entirely.
		if (!info.audible) {
			last_metrics.emitters_skipped++;
			continue;
		}

		// Compute the update interval for this emitter based on distance.
		// Near emitters: update at base_rate_hz.
		// Far emitters: update up to far_multiplier× slower.
		float distance = Math::sqrt(info.distance_sq);
		float distance_factor = CLAMP(distance / MAX(config.near_distance, 0.1f), 1.0f, config.far_multiplier);
		float emitter_interval = base_interval * distance_factor;

		// Is this emitter due for an update?
		if (info.last_update_time >= emitter_interval) {
			candidates.push_back(i);
		}
	}

	last_metrics.total_active = total_active;

	// If no candidates are due, nothing to do.
	if (candidates.is_empty()) {
		return 0;
	}

	// Sort candidates by priority: lower distance² (nearer) first, then higher importance.
	// Simple insertion sort — candidate count is bounded by MAX_EMITTERS (256).
	for (int i = 1; i < candidates.size(); i++) {
		int key = candidates[i];
		const EmitterInfo &key_info = p_emitters[key];
		int j = i - 1;
		while (j >= 0) {
			const EmitterInfo &cmp_info = p_emitters[candidates[j]];
			// Prioritize by: 1) nearer, 2) higher importance
			bool should_swap = key_info.distance_sq < cmp_info.distance_sq ||
					(key_info.distance_sq == cmp_info.distance_sq && key_info.importance > cmp_info.importance);
			if (!should_swap) break;
			candidates.write[j + 1] = candidates[j];
			j--;
		}
		candidates.write[j + 1] = key;
	}

	// Take up to max_emitters_this_frame from the sorted candidates.
	// Apply round-robin offset for fairness when budget doesn't cover all due emitters.
	int scheduled = 0;
	int num_candidates = candidates.size();

	// Adjust round-robin cursor.
	if (round_robin_cursor >= num_candidates) {
		round_robin_cursor = 0;
	}

	// First pass: take from cursor position (fairness for overflow).
	for (int k = 0; k < num_candidates && scheduled < max_emitters_this_frame; k++) {
		int idx = (round_robin_cursor + k) % num_candidates;
		r_to_update.push_back(p_emitters[candidates[idx]].emitter_index);
		scheduled++;
	}

	round_robin_cursor = (round_robin_cursor + scheduled) % MAX(num_candidates, 1);

	last_metrics.rays_issued = scheduled * config.rays_per_emitter;
	last_metrics.emitters_serviced = scheduled;
	last_metrics.emitters_skipped += (num_candidates - scheduled);

	return scheduled;
}
