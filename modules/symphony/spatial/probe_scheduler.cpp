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

	// How many total rays can we bill this frame (unified cost pool). Phase 5.1:
	// carry last frame's actual-vs-estimated error so the running average stays
	// within the ceiling — if solvers over-issued, next frame's pool shrinks.
	int cost_budget = config.ray_budget_per_frame;
	if (last_actual_rays > 0) {
		int overshoot = last_actual_rays - config.ray_budget_per_frame;
		if (overshoot > 0) {
			cost_budget = MAX(config.ray_budget_per_frame - overshoot, config.min_room_probe_budget);
		}
	}
	last_actual_rays = 0; // consume the correction; solvers will re-report.

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

	// Take from the top of the priority-sorted list, billing each emitter's
	// estimated_cost against the unified pool (Phase 5.1) rather than a fixed
	// per-emitter count. A round-robin start offset keeps things fair across
	// frames when the pool can't cover every due emitter; the offset walks the
	// PRIORITY order, so fairness is preserved (Phase 5.5). The first pick is
	// always serviced so a lone expensive emitter is never permanently starved.
	int scheduled = 0;
	int cost_spent = 0;
	int num_candidates = candidates.size();
	if (round_robin_cursor >= num_candidates) {
		round_robin_cursor = 0;
	}

	for (int k = 0; k < num_candidates; k++) {
		int idx = (round_robin_cursor + k) % num_candidates;
		const EmitterInfo &info = p_emitters[candidates[idx]];
		int cost = MAX(info.estimated_cost, 1);
		if (scheduled > 0 && cost_spent + cost > cost_budget) {
			continue; // try the next candidate; a cheaper one may still fit
		}
		r_to_update.push_back(info.emitter_index);
		cost_spent += cost;
		scheduled++;
		if (cost_spent >= cost_budget) {
			break;
		}
	}

	round_robin_cursor = (round_robin_cursor + scheduled) % MAX(num_candidates, 1);

	// rays_issued is the ESTIMATE; report_actual_rays() corrects next frame.
	last_metrics.rays_issued = cost_spent;
	last_metrics.emitters_serviced = scheduled;
	last_metrics.emitters_skipped += (num_candidates - scheduled);

	return scheduled;
}
