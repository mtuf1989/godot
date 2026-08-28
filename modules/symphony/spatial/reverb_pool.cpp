#include "reverb_pool.h"

ReverbPool::ReverbPool() {
	init(Config());
}

void ReverbPool::init(const Config &p_config) {
	config = p_config;
	config.active_slots = CLAMP(config.active_slots, 1, MAX_SLOTS);
	config.rt60_cluster_threshold = MAX(config.rt60_cluster_threshold, 0.001f);
	config.max_rt60 = MAX(config.max_rt60, 0.1f);
	reset();
}

void ReverbPool::reset() {
	for (int i = 0; i < MAX_SLOTS; i++) {
		slots[i] = SlotParams();
		slot_centroid_rt60[i] = 0.0f;
		slot_sum_rt60[i] = 0.0f;
		slot_sum_damping[i] = 0.0f;
		slot_member_count[i] = 0;
	}
	assignments.clear();
	metrics = Metrics();
	audio_server_touched = false;
}

float ReverbPool::_rt60_to_room_size(float p_rt60, float p_max_rt60) {
	// Map RT60 (seconds) → room_size (0..1). The FDN derives its delay-line
	// lengths from room_size; longer RT60 implies a larger space, so scale
	// linearly against the configured ceiling and clamp.
	float t = (p_max_rt60 > 0.001f) ? (p_rt60 / p_max_rt60) : 0.0f;
	return CLAMP(t, 0.01f, 1.0f);
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

int ReverbPool::assign(int p_emitter_id, float p_rt60, float p_damping, float p_reverb_send) {
	p_rt60 = CLAMP(p_rt60, 0.0f, config.max_rt60);
	p_damping = CLAMP(p_damping, 0.0f, 1.0f);
	p_reverb_send = CLAMP(p_reverb_send, 0.0f, 1.0f);

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
		a.active = true;
		assignments.insert(p_emitter_id, a);
		return target_slot;
	}

	// Existing emitter — update descriptor.
	existing->send = p_reverb_send;
	existing->rt60 = p_rt60;
	existing->damping = p_damping;

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
			slot_weight[a.slot] += w;
			slot_member_count[a.slot]++;
		}
		if (a.prev_slot >= 0 && a.prev_slot < MAX_SLOTS) {
			float pw = 1.0f - a.crossfade;
			slot_sum_rt60[a.prev_slot] += a.rt60 * pw;
			slot_sum_damping[a.prev_slot] += a.damping * pw;
			slot_weight[a.prev_slot] += pw;
			slot_member_count[a.prev_slot]++;
		}
	}

	// 3. Derive FDN parameters per slot from the weighted means.
	int active_slots_used = 0;
	for (int i = 0; i < MAX_SLOTS; i++) {
		if (i >= config.active_slots) {
			// Inactive slot — force idle.
			slots[i].wet_gain = 0.0f;
			slot_member_count[i] = 0;
			continue;
		}
		if (slot_weight[i] > 0.0001f) {
			float mean_rt60 = slot_sum_rt60[i] / slot_weight[i];
			float mean_damping = slot_sum_damping[i] / slot_weight[i];
			slot_centroid_rt60[i] = mean_rt60;
			slots[i].decay_time = CLAMP(mean_rt60, 0.1f, config.max_rt60);
			slots[i].damping = CLAMP(mean_damping, 0.0f, 1.0f);
			slots[i].room_size = _rt60_to_room_size(mean_rt60, config.max_rt60);
			// Wet gain ramps toward 1 with membership presence (simple: on).
			slots[i].wet_gain = 1.0f;
			active_slots_used++;
		} else {
			// Idle slot — fade wet to 0 so the tail decays out, keep centroid.
			slots[i].wet_gain = 0.0f;
			slot_member_count[i] = 0;
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
	r_send = a->send * a->crossfade;
	return true;
}

bool ReverbPool::emitter_prev_slot(int p_emitter_id, int &r_prev_slot, float &r_send) const {
	const EmitterAssignment *a = assignments.getptr(p_emitter_id);
	if (!a || a->prev_slot < 0) {
		return false;
	}
	r_prev_slot = a->prev_slot;
	r_send = a->send * (1.0f - a->crossfade);
	return true;
}

bool ReverbPool::is_migrating(int p_emitter_id) const {
	const EmitterAssignment *a = assignments.getptr(p_emitter_id);
	return a && a->prev_slot >= 0;
}
