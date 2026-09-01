#include "reverb_pool.h"

ReverbPool::ReverbPool() {
	init(Config());
}

void ReverbPool::init(const Config &p_config) {
	config = p_config;
	config.active_slots = CLAMP(config.active_slots, 1, MAX_SLOTS);
	config.rt60_cluster_threshold = MAX(config.rt60_cluster_threshold, 0.001f);
	config.max_rt60 = MAX(config.max_rt60, 0.1f);
	config.rt60_cluster_hysteresis = MAX(config.rt60_cluster_hysteresis, 0.0f);
	config.min_dwell_seconds = MAX(config.min_dwell_seconds, 0.0f);
	reset();
}

void ReverbPool::reset() {
	for (int i = 0; i < MAX_SLOTS; i++) {
		slots[i] = SlotParams();
		slot_centroid_rt60[i] = 0.0f;
		slot_sum_rt60[i] = 0.0f;
		slot_sum_damping[i] = 0.0f;
		slot_sum_volume[i] = 0.0f;
		slot_member_count[i] = 0;
	}
	assignments.clear();
	metrics = Metrics();
	// NOTE (Phase 5.5): audio_server_touched is deliberately NOT cleared here.
	// reset() runs on every init()/set_reverb_pool_slots() at runtime; clearing
	// the flag would let a mid-session reconfigure silently re-arm the R4
	// anti-regression guard. It stays sticky for the pool's lifetime.
}

float ReverbPool::_rt60_to_room_size(float p_rt60, float p_max_rt60) {
	// Fallback room_size when no volume estimate is available: map RT60 →
	// room_size linearly against the ceiling. Volume-based mapping (Phase 4.4)
	// is preferred and used whenever an emitter carries a volume estimate.
	float t = (p_max_rt60 > 0.001f) ? (p_rt60 / p_max_rt60) : 0.0f;
	return CLAMP(t, 0.01f, 1.0f);
}

float ReverbPool::_volume_to_room_size(float p_volume) {
	// Phase 4.4: room_size ~ characteristic room dimension / reference dimension.
	// cbrt(volume) is the side length of the equivalent cube; normalize against
	// a reference of ~30 m (a large hall) so typical rooms land mid-range and a
	// small closet is near the floor.
	static const float REFERENCE_DIMENSION = 30.0f;
	if (p_volume <= 0.0f) {
		return 0.0f; // caller falls back to the RT60 estimate
	}
	float dim = Math::pow(p_volume, 1.0f / 3.0f);
	return CLAMP(dim / REFERENCE_DIMENSION, 0.01f, 1.0f);
}

int ReverbPool::_find_slot_for(float p_rt60, bool &r_degraded) {
	r_degraded = false;

	// 1. Prefer an existing occupied slot whose centroid RT60 is within the
	//    cluster threshold (nearest centroid wins).
	int best_occupied = -1;
	float best_occupied_dist = config.rt60_cluster_threshold;
	for (int i = 0; i < config.active_slots; i++) {
		if (slot_member_count[i] <= 0) {
			continue;
		}
		float d = Math::abs(slot_centroid_rt60[i] - p_rt60);
		if (d <= best_occupied_dist) {
			best_occupied_dist = d;
			best_occupied = i;
		}
	}
	if (best_occupied >= 0) {
		return best_occupied;
	}

	// 2. No nearby cluster — grab a free slot to seed a new cluster.
	for (int i = 0; i < config.active_slots; i++) {
		if (slot_member_count[i] <= 0) {
			return i;
		}
	}

	// 3. Pool full — degrade gracefully to the nearest cluster regardless of
	//    threshold (never allocate at runtime, never churn buses).
	r_degraded = true;
	int nearest = 0;
	float nearest_dist = Math::abs(slot_centroid_rt60[0] - p_rt60);
	for (int i = 1; i < config.active_slots; i++) {
		float d = Math::abs(slot_centroid_rt60[i] - p_rt60);
		if (d < nearest_dist) {
			nearest_dist = d;
			nearest = i;
		}
	}
	return nearest;
}

int ReverbPool::assign(int p_emitter_id, float p_rt60, float p_damping, float p_reverb_send, float p_volume) {
	p_rt60 = CLAMP(p_rt60, 0.0f, config.max_rt60);
	p_damping = CLAMP(p_damping, 0.0f, 1.0f);
	p_reverb_send = CLAMP(p_reverb_send, 0.0f, 1.0f);
	p_volume = MAX(p_volume, 0.0f);

	EmitterAssignment *existing = assignments.getptr(p_emitter_id);

	bool degraded = false;
	int target_slot = _find_slot_for(p_rt60, degraded);
	if (degraded) {
		metrics.degraded_assignments++;
	}

	// Provisional occupancy: if the chosen slot is currently empty, seed its
	// centroid and mark it occupied immediately so that other emitters
	// assigned *in the same frame* (before update() recomputes) see it as a
	// live cluster and don't all collapse onto the first free slot. update()
	// recomputes the accurate weighted centroid afterward.
	if (target_slot >= 0 && slot_member_count[target_slot] <= 0) {
		slot_centroid_rt60[target_slot] = p_rt60;
		slot_member_count[target_slot] = 1;
	}

	if (existing == nullptr) {
		// New emitter — snap onto the target slot (no crossfade on first
		// assignment; UE5 "first check instant" pattern).
		EmitterAssignment a;
		a.slot = target_slot;
		a.prev_slot = -1;
		a.crossfade = 1.0f;
		a.send = p_reverb_send;
		a.rt60 = p_rt60;
		a.damping = p_damping;
		a.volume = p_volume;
		a.active = true;
		assignments.insert(p_emitter_id, a);
		return target_slot;
	}

	// Existing emitter — update descriptor.
	existing->send = p_reverb_send;
	existing->rt60 = p_rt60;
	existing->damping = p_damping;
	existing->volume = p_volume;

	// Phase 4.3 — hysteresis + dwell to stop a boundary emitter oscillating
	// between two slots (centroids drift and assign() runs every frame). Only
	// migrate if the emitter has dwelt on its current slot at least
	// min_dwell_seconds AND the candidate is a clearly better RT60 match (beats
	// the current slot's distance by more than rt60_cluster_hysteresis).
	if (target_slot != existing->slot && existing->slot >= 0 && existing->slot < config.active_slots) {
		const float cur_dist = Math::abs(slot_centroid_rt60[existing->slot] - p_rt60);
		const float cand_dist = Math::abs(slot_centroid_rt60[target_slot] - p_rt60);
		const bool dwell_ok = existing->time_on_slot >= config.min_dwell_seconds;
		const bool clearly_better = cand_dist < cur_dist - config.rt60_cluster_hysteresis;
		if (!dwell_ok || !clearly_better) {
			target_slot = existing->slot; // keep current slot
		}
	}

	if (target_slot != existing->slot) {
		// Migration — start (or restart) a crossfade from the current slot.
		// If already mid-crossfade, keep fading from the original prev_slot so
		// we never leave a slot abruptly.
		if (existing->prev_slot == target_slot) {
			// Migrating back to the slot we were fading out of — swap and
			// invert the crossfade so the transition stays continuous.
			int old_target = existing->slot;
			existing->slot = target_slot;
			existing->prev_slot = old_target;
			existing->crossfade = 1.0f - existing->crossfade;
			existing->time_on_slot = 0.0f;
			metrics.migrations++;
		} else {
			if (existing->prev_slot < 0) {
				existing->prev_slot = existing->slot;
			}
			existing->slot = target_slot;
			if (existing->prev_slot == existing->slot) {
				// Degenerate: nothing to crossfade.
				existing->prev_slot = -1;
				existing->crossfade = 1.0f;
			} else {
				existing->crossfade = 0.0f;
				existing->time_on_slot = 0.0f;
				metrics.migrations++;
			}
		}
	}

	return existing->slot;
}

void ReverbPool::release(int p_emitter_id) {
	assignments.erase(p_emitter_id);
}

void ReverbPool::update(float p_delta) {
	// 1. Advance crossfades.
	const float rate = _crossfade_rate();
	for (KeyValue<int, EmitterAssignment> &kv : assignments) {
		EmitterAssignment &a = kv.value;
		a.time_on_slot += p_delta; // Phase 4.3 dwell timer.
		if (a.prev_slot >= 0 && a.crossfade < 1.0f) {
			a.crossfade += rate * p_delta;
			if (a.crossfade >= 1.0f) {
				a.crossfade = 1.0f;
				a.prev_slot = -1; // Migration complete.
			}
		}
	}

	// 2. Recompute per-slot aggregation from assigned emitters.
	//    An emitter contributes to its target slot (weighted by crossfade) and,
	//    while migrating, to its prev_slot (weighted by 1-crossfade) so slot
	//    parameters track the membership smoothly.
	for (int i = 0; i < MAX_SLOTS; i++) {
		slot_sum_rt60[i] = 0.0f;
		slot_sum_damping[i] = 0.0f;
		slot_sum_volume[i] = 0.0f;
		slot_member_count[i] = 0;
	}

	float slot_weight[MAX_SLOTS] = {};
	int assigned = 0;
	for (const KeyValue<int, EmitterAssignment> &kv : assignments) {
		const EmitterAssignment &a = kv.value;
		if (!a.active || a.slot < 0) {
			continue;
		}
		assigned++;

		float w = a.crossfade;
		if (a.slot >= 0 && a.slot < MAX_SLOTS) {
			slot_sum_rt60[a.slot] += a.rt60 * w;
			slot_sum_damping[a.slot] += a.damping * w;
			slot_sum_volume[a.slot] += a.volume * w;
			slot_weight[a.slot] += w;
			slot_member_count[a.slot]++;
		}
		if (a.prev_slot >= 0 && a.prev_slot < MAX_SLOTS) {
			float pw = 1.0f - a.crossfade;
			slot_sum_rt60[a.prev_slot] += a.rt60 * pw;
			slot_sum_damping[a.prev_slot] += a.damping * pw;
			slot_sum_volume[a.prev_slot] += a.volume * pw;
			slot_weight[a.prev_slot] += pw;
			slot_member_count[a.prev_slot]++;
		}
	}

	// 3. Derive FDN parameters per slot from the weighted means.
	//    wet_gain RAMPS toward its target (1 = has members, 0 = idle) at
	//    1/crossfade_seconds per second, so a slot losing its last member fades
	//    its reverberant tail out instead of truncating it (and a newly occupied
	//    slot fades in). Everything else is set instantly.
	const float wet_rate = _crossfade_rate();
	int active_slots_used = 0;
	for (int i = 0; i < MAX_SLOTS; i++) {
		if (i >= config.active_slots) {
			// Inactive slot — force idle (hard, it is not a real slot).
			slots[i].wet_gain = 0.0f;
			slot_member_count[i] = 0;
			continue;
		}
		float wet_target;
		if (slot_weight[i] > 0.0001f) {
			float mean_rt60 = slot_sum_rt60[i] / slot_weight[i];
			float mean_damping = slot_sum_damping[i] / slot_weight[i];
			slot_centroid_rt60[i] = mean_rt60;
			slots[i].decay_time = CLAMP(mean_rt60, 0.1f, config.max_rt60);
			slots[i].damping = CLAMP(mean_damping, 0.0f, 1.0f);
			// Phase 4.4: prefer volume-derived room_size; fall back to the RT60
			// map when no volume estimate is present for this slot.
			float mean_volume = slot_sum_volume[i] / slot_weight[i];
			float vol_size = _volume_to_room_size(mean_volume);
			slots[i].room_size = (vol_size > 0.0f) ? vol_size : _rt60_to_room_size(mean_rt60, config.max_rt60);
			wet_target = 1.0f;
			active_slots_used++;
		} else {
			// Idle slot — target 0 so the tail fades out; keep centroid + params
			// so the fade-out uses the last real reverb settings.
			wet_target = 0.0f;
			slot_member_count[i] = 0;
		}
		// Ramp wet_gain toward the target.
		if (slots[i].wet_gain < wet_target) {
			slots[i].wet_gain = MIN(slots[i].wet_gain + wet_rate * p_delta, wet_target);
		} else if (slots[i].wet_gain > wet_target) {
			slots[i].wet_gain = MAX(slots[i].wet_gain - wet_rate * p_delta, wet_target);
		}
	}

	metrics.active_slot_count = active_slots_used;
	metrics.assigned_emitters = assigned;
}

const ReverbPool::SlotParams &ReverbPool::slot_params(int p_slot) const {
	static const SlotParams k_default = SlotParams();
	if (p_slot < 0 || p_slot >= MAX_SLOTS) {
		return k_default;
	}
	return slots[p_slot];
}

bool ReverbPool::emitter_slot(int p_emitter_id, int &r_slot, float &r_send) const {
	const EmitterAssignment *a = assignments.getptr(p_emitter_id);
	if (!a || a->slot < 0) {
		return false;
	}
	r_slot = a->slot;
	// Equal-power (constant-energy) crossfade: two decorrelated reverb tails sum
	// in power, not amplitude, so linear weighting dips ~3 dB at the midpoint.
	// sqrt keeps perceived loudness constant across a migration.
	r_send = a->send * Math::sqrt(a->crossfade);
	return true;
}

bool ReverbPool::emitter_prev_slot(int p_emitter_id, int &r_prev_slot, float &r_send) const {
	const EmitterAssignment *a = assignments.getptr(p_emitter_id);
	if (!a || a->prev_slot < 0) {
		return false;
	}
	r_prev_slot = a->prev_slot;
	r_send = a->send * Math::sqrt(1.0f - a->crossfade);
	return true;
}

bool ReverbPool::is_migrating(int p_emitter_id) const {
	const EmitterAssignment *a = assignments.getptr(p_emitter_id);
	return a && a->prev_slot >= 0;
}
