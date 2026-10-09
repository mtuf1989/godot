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
		slot_sum_abs_high[i] = 0.0f;
		slot_sum_volume[i] = 0.0f;
		slot_sum_dim_x[i] = 0.0f;
		slot_sum_dim_y[i] = 0.0f;
		slot_sum_dim_z[i] = 0.0f;
		slot_sum_authored[i] = 0.0f;
		slot_member_count[i] = 0;
	}
	assignments.clear();
	metrics = Metrics();
}

Vector3 ReverbPool::_aspect_dims_from_volume(float p_volume) {
	if (p_volume <= 0.0f) {
		return Vector3();
	}
	const float side = Math::pow(p_volume, 1.0f / 3.0f);
	return Vector3(1.25f * side, 0.64f * side, 1.25f * side);
}

void ReverbPool::_derive_slot_acoustics(float p_volume, float p_rt60, float p_abs_high, const Vector3 &p_dims,
		float p_drr_bias_db, SlotParams &r_out) {
	r_out.decay_time = CLAMP(p_rt60, 0.1f, 20.0f);
	r_out.absorption_high = CLAMP(p_abs_high, 0.01f, 1.0f);
	r_out.volume = MAX(p_volume, 0.0f);

	Vector3 dims = p_dims;
	if (dims.x <= 0.0f || dims.y <= 0.0f || dims.z <= 0.0f) {
		dims = _aspect_dims_from_volume(p_volume);
	}
	r_out.dim_x = dims.x;
	r_out.dim_y = dims.y;
	r_out.dim_z = dims.z;

	float V = r_out.volume;
	if (V <= 0.0f && dims.x > 0.0f) {
		V = dims.x * dims.y * dims.z;
		r_out.volume = V;
	}

	float alpha_mean = 0.3f;
	if (V > 0.0f && dims.x > 0.0f && r_out.decay_time > 0.01f) {
		const float S = 2.0f * (dims.x * dims.y + dims.x * dims.z + dims.y * dims.z);
		if (S > 0.01f) {
			alpha_mean = CLAMP(0.161f * V / (S * r_out.decay_time), 0.01f, 0.99f);
		}
	}
	r_out.hf_ratio = CLAMP(alpha_mean / r_out.absorption_high, 0.1f, 1.0f);
	r_out.er_reflection = Math::sqrt(CLAMP(1.0f - alpha_mean, 0.0f, 1.0f));
	r_out.critical_distance = SymphonyReverbLevel::critical_distance(V > 0.0f ? V : 1.0f, r_out.decay_time, p_drr_bias_db);
}

int ReverbPool::_find_slot_for(float p_rt60, bool &r_degraded) {
	r_degraded = false;

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

	for (int i = 0; i < config.active_slots; i++) {
		if (slot_member_count[i] <= 0) {
			return i;
		}
	}

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

int ReverbPool::assign(int p_emitter_id, float p_rt60, float p_absorption_high, float p_reverb_send,
		float p_volume, const Vector3 &p_dims) {
	p_rt60 = CLAMP(p_rt60, 0.0f, config.max_rt60);
	p_absorption_high = CLAMP(p_absorption_high, 0.0f, 1.0f);
	p_reverb_send = CLAMP(p_reverb_send, 0.0f, 1.0f);
	p_volume = MAX(p_volume, 0.0f);

	EmitterAssignment *existing = assignments.getptr(p_emitter_id);

	bool degraded = false;
	int target_slot = _find_slot_for(p_rt60, degraded);
	if (degraded) {
		metrics.degraded_assignments++;
	}

	if (target_slot >= 0 && slot_member_count[target_slot] <= 0) {
		slot_centroid_rt60[target_slot] = p_rt60;
		slot_member_count[target_slot] = 1;
	}

	if (existing == nullptr) {
		EmitterAssignment a;
		a.slot = target_slot;
		a.prev_slot = -1;
		a.crossfade = 1.0f;
		a.send = p_reverb_send;
		a.rt60 = p_rt60;
		a.absorption_high = p_absorption_high;
		a.volume = p_volume;
		a.dims = p_dims;
		a.active = true;
		assignments.insert(p_emitter_id, a);
		return target_slot;
	}

	existing->send = p_reverb_send;
	existing->rt60 = p_rt60;
	existing->absorption_high = p_absorption_high;
	existing->volume = p_volume;
	existing->dims = p_dims;

	if (target_slot != existing->slot && existing->slot >= 0 && existing->slot < config.active_slots) {
		const float cur_dist = Math::abs(slot_centroid_rt60[existing->slot] - p_rt60);
		const float cand_dist = Math::abs(slot_centroid_rt60[target_slot] - p_rt60);
		const bool dwell_ok = existing->time_on_slot >= config.min_dwell_seconds;
		const bool clearly_better = cand_dist < cur_dist - config.rt60_cluster_hysteresis;
		if (!dwell_ok || !clearly_better) {
			target_slot = existing->slot;
		}
	}

	if (target_slot != existing->slot) {
		if (existing->prev_slot == target_slot) {
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
	const float rate = _crossfade_rate();
	for (KeyValue<int, EmitterAssignment> &kv : assignments) {
		EmitterAssignment &a = kv.value;
		a.time_on_slot += p_delta;
		if (a.prev_slot >= 0 && a.crossfade < 1.0f) {
			a.crossfade += rate * p_delta;
			if (a.crossfade >= 1.0f) {
				a.crossfade = 1.0f;
				a.prev_slot = -1;
			}
		}
	}

	for (int i = 0; i < MAX_SLOTS; i++) {
		slot_sum_rt60[i] = 0.0f;
		slot_sum_abs_high[i] = 0.0f;
		slot_sum_volume[i] = 0.0f;
		slot_sum_dim_x[i] = 0.0f;
		slot_sum_dim_y[i] = 0.0f;
		slot_sum_dim_z[i] = 0.0f;
		slot_sum_authored[i] = 0.0f;
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

		auto accumulate = [&](int s, float w) {
			if (s < 0 || s >= MAX_SLOTS) {
				return;
			}
			slot_sum_rt60[s] += a.rt60 * w;
			slot_sum_abs_high[s] += a.absorption_high * w;
			slot_sum_volume[s] += a.volume * w;
			slot_weight[s] += w;
			slot_member_count[s]++;
			if (a.dims.x > 0.0f && a.dims.y > 0.0f && a.dims.z > 0.0f) {
				slot_sum_dim_x[s] += a.dims.x * w;
				slot_sum_dim_y[s] += a.dims.y * w;
				slot_sum_dim_z[s] += a.dims.z * w;
				slot_sum_authored[s] += w;
			}
		};

		accumulate(a.slot, a.crossfade);
		if (a.prev_slot >= 0) {
			accumulate(a.prev_slot, 1.0f - a.crossfade);
		}
	}

	int active_slots_used = 0;
	for (int i = 0; i < MAX_SLOTS; i++) {
		if (i >= config.active_slots) {
			slot_member_count[i] = 0;
			continue;
		}
		if (slot_weight[i] > 0.0001f) {
			const float mean_rt60 = slot_sum_rt60[i] / slot_weight[i];
			const float mean_abs = slot_sum_abs_high[i] / slot_weight[i];
			const float mean_vol = slot_sum_volume[i] / slot_weight[i];
			slot_centroid_rt60[i] = mean_rt60;
			Vector3 dims;
			if (slot_sum_authored[i] > 0.0001f) {
				dims.x = slot_sum_dim_x[i] / slot_sum_authored[i];
				dims.y = slot_sum_dim_y[i] / slot_sum_authored[i];
				dims.z = slot_sum_dim_z[i] / slot_sum_authored[i];
			}
			_derive_slot_acoustics(mean_vol, mean_rt60, mean_abs, dims, config.drr_bias_db, slots[i]);
			active_slots_used++;
		} else {
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

bool ReverbPool::emitter_descriptor(int p_emitter_id, float &r_rt60, float &r_volume, Vector3 &r_dims) const {
	const EmitterAssignment *a = assignments.getptr(p_emitter_id);
	if (!a) {
		return false;
	}
	r_rt60 = a->rt60;
	r_volume = a->volume;
	r_dims = a->dims;
	return true;
}
