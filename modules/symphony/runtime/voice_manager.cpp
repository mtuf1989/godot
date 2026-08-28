#include "voice_manager.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/math/math_funcs.h"
#include "core/variant/dictionary.h"
#include "core/variant/array.h"

#include <cfloat>

SymphonyVoicePool *SymphonyVoicePool::singleton = nullptr;

SymphonyVoicePool::SymphonyVoicePool() {
	singleton = this;
	pool_size = GLOBAL_DEF("audio/symphony/voice_pool_size", 48);
	ProjectSettings::get_singleton()->set_custom_property_info(PropertyInfo(
			Variant::INT, "audio/symphony/voice_pool_size", PROPERTY_HINT_RANGE, "8,128,1"));
	slots = memnew_arr(VoiceSlot, pool_size);
	slot_attenuation_curves = memnew_arr(Ref<Curve>, pool_size);
	VoiceMetrics m;
	metrics.store(m, std::memory_order_relaxed);
}

SymphonyVoicePool::~SymphonyVoicePool() {
	if (slots) {
		memdelete_arr(slots);
		slots = nullptr;
	}
	if (slot_attenuation_curves) {
		memdelete_arr(slot_attenuation_curves);
		slot_attenuation_curves = nullptr;
	}
	singleton = nullptr;
}

void SymphonyVoicePool::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_active_voice_count"), &SymphonyVoicePool::get_active_voice_count);
	ClassDB::bind_method(D_METHOD("get_virtual_voice_count"), &SymphonyVoicePool::get_virtual_voice_count);
	ClassDB::bind_method(D_METHOD("get_stolen_this_frame"), &SymphonyVoicePool::get_stolen_this_frame);
	ClassDB::bind_method(D_METHOD("get_budget_percent"), &SymphonyVoicePool::get_budget_percent);
	ClassDB::bind_method(D_METHOD("get_pool_size"), &SymphonyVoicePool::get_pool_size);
	ClassDB::bind_method(D_METHOD("get_slot_state", "index"), &SymphonyVoicePool::get_slot_state);
	ClassDB::bind_method(D_METHOD("acquire_slot", "priority"), &SymphonyVoicePool::acquire_slot);
	ClassDB::bind_method(D_METHOD("release_slot", "index", "immediate"), &SymphonyVoicePool::release_slot, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("reclaim_slot", "index", "priority", "steal_reason"), &SymphonyVoicePool::reclaim_slot, DEFVAL(StringName()));
	ClassDB::bind_method(D_METHOD("set_slot_rms", "index", "rms"), &SymphonyVoicePool::set_slot_rms);
	ClassDB::bind_method(D_METHOD("is_slot_rms_valid", "index"), &SymphonyVoicePool::is_slot_rms_valid);
	ClassDB::bind_method(D_METHOD("virtualize", "index"), &SymphonyVoicePool::virtualize);
	ClassDB::bind_method(D_METHOD("devirtualize", "index"), &SymphonyVoicePool::devirtualize);
	ClassDB::bind_method(D_METHOD("process_frame"), &SymphonyVoicePool::process_frame);
	ClassDB::bind_method(D_METHOD("set_local_parameter", "slot", "name", "value"), &SymphonyVoicePool::set_local_parameter);
	ClassDB::bind_method(D_METHOD("get_local_parameter", "slot", "name"), &SymphonyVoicePool::get_local_parameter);
	ClassDB::bind_method(D_METHOD("has_local_parameter", "slot", "name"), &SymphonyVoicePool::has_local_parameter);
	ClassDB::bind_method(D_METHOD("clear_local_parameters", "slot"), &SymphonyVoicePool::clear_local_parameters);

	// Importance-based mixing
	ClassDB::bind_method(D_METHOD("set_listener_position", "position"), &SymphonyVoicePool::set_listener_position);
	ClassDB::bind_method(D_METHOD("get_listener_position"), &SymphonyVoicePool::get_listener_position);
	ClassDB::bind_method(D_METHOD("set_reference_distance", "distance"), &SymphonyVoicePool::set_reference_distance);
	ClassDB::bind_method(D_METHOD("get_reference_distance"), &SymphonyVoicePool::get_reference_distance);
	ClassDB::bind_method(D_METHOD("set_slot_category", "slot", "category"), &SymphonyVoicePool::set_slot_category);
	ClassDB::bind_method(D_METHOD("set_slot_importance_weight", "slot", "weight"), &SymphonyVoicePool::set_slot_importance_weight);
	ClassDB::bind_method(D_METHOD("set_slot_position", "slot", "position"), &SymphonyVoicePool::set_slot_position);
	ClassDB::bind_method(D_METHOD("get_slot_importance", "slot"), &SymphonyVoicePool::get_slot_importance);
	ClassDB::bind_method(D_METHOD("update_importance"), &SymphonyVoicePool::update_importance);
	ClassDB::bind_method(D_METHOD("update_importance_all"), &SymphonyVoicePool::update_importance_all);

	// Distance attenuation
	ClassDB::bind_method(D_METHOD("set_slot_spatial_mode", "slot", "mode"), &SymphonyVoicePool::set_slot_spatial_mode);
	ClassDB::bind_method(D_METHOD("set_slot_attenuation_model", "slot", "model"), &SymphonyVoicePool::set_slot_attenuation_model);
	ClassDB::bind_method(D_METHOD("set_slot_max_distance", "slot", "distance"), &SymphonyVoicePool::set_slot_max_distance);
	ClassDB::bind_method(D_METHOD("set_slot_inner_radius", "slot", "radius"), &SymphonyVoicePool::set_slot_inner_radius);
	ClassDB::bind_method(D_METHOD("set_slot_falloff_distance", "slot", "distance"), &SymphonyVoicePool::set_slot_falloff_distance);
	ClassDB::bind_method(D_METHOD("set_slot_virtualize_when_inaudible", "slot", "virtualize"), &SymphonyVoicePool::set_slot_virtualize_when_inaudible);
	ClassDB::bind_method(D_METHOD("get_slot_attenuation_volume", "slot"), &SymphonyVoicePool::get_slot_attenuation_volume);
	ClassDB::bind_method(D_METHOD("set_slot_attenuation_curve", "slot", "curve"), &SymphonyVoicePool::set_slot_attenuation_curve);
	ClassDB::bind_method(D_METHOD("get_slot_attenuation_curve", "slot"), &SymphonyVoicePool::get_slot_attenuation_curve);
	ClassDB::bind_method(D_METHOD("set_slot_start_delay", "slot", "delay_s"), &SymphonyVoicePool::set_slot_start_delay);
	ClassDB::bind_method(D_METHOD("get_slot_start_delay", "slot"), &SymphonyVoicePool::get_slot_start_delay);
	ClassDB::bind_method(D_METHOD("is_slot_start_pending", "slot"), &SymphonyVoicePool::is_slot_start_pending);
	ClassDB::bind_method(D_METHOD("tick_deferred_starts", "delta"), &SymphonyVoicePool::tick_deferred_starts);

	BIND_ENUM_CONSTANT(VOICE_FREE);
	BIND_ENUM_CONSTANT(VOICE_TO_PLAY);
	BIND_ENUM_CONSTANT(VOICE_PLAYING);
	BIND_ENUM_CONSTANT(VOICE_VIRTUALIZING);
	BIND_ENUM_CONSTANT(VOICE_VIRTUAL);
	BIND_ENUM_CONSTANT(VOICE_DEVIRTUALIZING);
	BIND_ENUM_CONSTANT(VOICE_STOPPING);
	BIND_ENUM_CONSTANT(VOICE_STOPPED);

	// Event log
	ClassDB::bind_method(D_METHOD("log_event", "event_name", "result", "slot", "importance", "steal_reason"), &SymphonyVoicePool::log_event, DEFVAL(StringName()));
	ClassDB::bind_method(D_METHOD("get_recent_events", "count"), &SymphonyVoicePool::get_recent_events, DEFVAL(20));
	ClassDB::bind_method(D_METHOD("get_event_log_count"), &SymphonyVoicePool::get_event_log_count);
	ClassDB::bind_method(D_METHOD("get_category_voice_counts"), &SymphonyVoicePool::get_category_voice_counts);

	// LOD control
	ClassDB::bind_method(D_METHOD("force_lod", "slot", "lod_tier"), &SymphonyVoicePool::force_lod);
	ClassDB::bind_method(D_METHOD("release_lod_force", "slot"), &SymphonyVoicePool::release_lod_force);
	ClassDB::bind_method(D_METHOD("set_slot_lod_thresholds", "slot", "threshold_1", "threshold_2"), &SymphonyVoicePool::set_slot_lod_thresholds);
	ClassDB::bind_method(D_METHOD("get_slot_current_lod", "slot"), &SymphonyVoicePool::get_slot_current_lod);
	ClassDB::bind_method(D_METHOD("get_slot_target_lod", "slot"), &SymphonyVoicePool::get_slot_target_lod);

	BIND_ENUM_CONSTANT(EVENT_PLAYED);
	BIND_ENUM_CONSTANT(EVENT_STOLEN);
	BIND_ENUM_CONSTANT(EVENT_REJECTED_COOLDOWN);
	BIND_ENUM_CONSTANT(EVENT_REJECTED_VOICE_LIMIT);
	BIND_ENUM_CONSTANT(EVENT_REJECTED_NO_STREAMS);
	BIND_ENUM_CONSTANT(EVENT_VIRTUALIZED);
	BIND_ENUM_CONSTANT(EVENT_DEVIRTUALIZED);

	// Signals for virtualization transitions (Phase C: enables graph memory release)
	ADD_SIGNAL(MethodInfo("voice_virtualized", PropertyInfo(Variant::INT, "slot_index")));
	ADD_SIGNAL(MethodInfo("voice_devirtualized", PropertyInfo(Variant::INT, "slot_index")));
}

static void _activate_slot(SymphonyVoicePool::VoiceSlot &slot, int p_priority) {
	slot.state = SymphonyVoicePool::VOICE_TO_PLAY;
	slot.priority = p_priority;
	slot.importance = (float)p_priority;
	slot.rms = 0.0f;
	slot.rms_valid = false;
	slot.fade_progress = 0.0f;
	slot.fade_speed = 1.0f / SymphonyVoicePool::ANTI_CLICK_SAMPLES;
	slot.start_time = OS::get_singleton()->get_ticks_usec();
	slot.local_param_count = 0;
	slot.attenuation_volume = 1.0f;
	slot.pending_start_delay_s = 0.0f;
	slot.lod_threshold_1 = 0.3f;
	slot.lod_threshold_2 = 0.7f;
	slot.current_lod = 0;
	slot.target_lod = 0;
	slot.lod_forced = false;
}

int SymphonyVoicePool::acquire_slot(int p_priority) {
	// Free-only: EventDispatcher owns stealing (plan §10).
	for (int i = 0; i < pool_size; i++) {
		if (slots[i].state == VOICE_FREE) {
			_activate_slot(slots[i], p_priority);
			slot_attenuation_curves[i] = Ref<Curve>();
			return i;
		}
	}
	return -1;
}

void SymphonyVoicePool::release_slot(int p_index, bool p_immediate) {
	ERR_FAIL_INDEX(p_index, pool_size);
	if (p_immediate) {
		slots[p_index].state = VOICE_FREE;
		slots[p_index].event_id = 0;
		slots[p_index].rms_valid = false;
		slots[p_index].inner_radius = 0.0f;
		slots[p_index].falloff_distance = 0.0f;
		slots[p_index].pending_start_delay_s = 0.0f;
	} else {
		slots[p_index].state = VOICE_STOPPING;
		slots[p_index].fade_progress = 1.0f;
		slots[p_index].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
	}
}

void SymphonyVoicePool::reclaim_slot(int p_index, int p_priority, const StringName &p_steal_reason) {
	ERR_FAIL_INDEX(p_index, pool_size);
	(void)p_steal_reason;
	_activate_slot(slots[p_index], p_priority);
	slots[p_index].event_id = 0;
	slot_attenuation_curves[p_index] = Ref<Curve>();
	stolen_this_frame++;
}

int SymphonyVoicePool::find_lowest_importance_slot() const {
	int worst_idx = -1;
	float worst_importance = FLT_MAX;
	for (int i = 0; i < pool_size; i++) {
		if (slots[i].state == VOICE_PLAYING || slots[i].state == VOICE_TO_PLAY) {
			if (slots[i].importance < worst_importance ||
					(slots[i].importance == worst_importance && (worst_idx < 0 || i < worst_idx))) {
				worst_importance = slots[i].importance;
				worst_idx = i;
			}
		}
	}
	return worst_idx;
}

int SymphonyVoicePool::steal_lowest_importance() {
	// Select-only (compat). Prefer EventDispatcher + reclaim_slot for correct event counts.
	return find_lowest_importance_slot();
}

void SymphonyVoicePool::set_slot_rms(int p_index, float p_rms) {
	ERR_FAIL_INDEX(p_index, pool_size);
	slots[p_index].rms = p_rms;
	slots[p_index].rms_valid = true;
}

bool SymphonyVoicePool::is_slot_rms_valid(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, pool_size, false);
	return slots[p_index].rms_valid;
}

void SymphonyVoicePool::virtualize(int p_index) {
	ERR_FAIL_INDEX(p_index, pool_size);
	if (slots[p_index].state == VOICE_PLAYING) {
		slots[p_index].state = VOICE_VIRTUALIZING;
		slots[p_index].fade_progress = 1.0f;
		slots[p_index].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
	}
}

void SymphonyVoicePool::devirtualize(int p_index) {
	ERR_FAIL_INDEX(p_index, pool_size);
	if (slots[p_index].state == VOICE_VIRTUAL) {
		slots[p_index].state = VOICE_DEVIRTUALIZING;
		slots[p_index].fade_progress = 0.0f;
		slots[p_index].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
	}
}

SymphonyVoicePool::VoiceState SymphonyVoicePool::get_slot_state(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, pool_size, VOICE_FREE);
	return slots[p_index].state;
}

SymphonyVoicePool::VoiceSlot *SymphonyVoicePool::get_slot(int p_index) {
	ERR_FAIL_INDEX_V(p_index, pool_size, nullptr);
	return &slots[p_index];
}

const SymphonyVoicePool::VoiceSlot *SymphonyVoicePool::get_slot(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, pool_size, nullptr);
	return &slots[p_index];
}

int SymphonyVoicePool::get_active_voice_count() const {
	VoiceMetrics m = metrics.load(std::memory_order_relaxed);
	return m.active;
}

int SymphonyVoicePool::get_virtual_voice_count() const {
	VoiceMetrics m = metrics.load(std::memory_order_relaxed);
	return m.virtual_count;
}

int SymphonyVoicePool::get_stolen_this_frame() const {
	VoiceMetrics m = metrics.load(std::memory_order_relaxed);
	return m.stolen_this_frame;
}

float SymphonyVoicePool::get_budget_percent() const {
	VoiceMetrics m = metrics.load(std::memory_order_relaxed);
	return m.budget_percent;
}

void SymphonyVoicePool::set_local_parameter(int p_slot, const StringName &p_name, float p_value) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	VoiceSlot &slot = slots[p_slot];

	// Check if param already exists
	for (int i = 0; i < slot.local_param_count; i++) {
		if (slot.local_param_names[i] == p_name) {
			slot.local_param_values[i] = p_value;
			return;
		}
	}

	// Add new param
	ERR_FAIL_COND_MSG(slot.local_param_count >= MAX_LOCAL_PARAMS,
			"VoiceSlot: Maximum local parameter count reached.");
	slot.local_param_names[slot.local_param_count] = p_name;
	slot.local_param_values[slot.local_param_count] = p_value;
	slot.local_param_count++;
}

float SymphonyVoicePool::get_local_parameter(int p_slot, const StringName &p_name) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, 0.0f);
	const VoiceSlot &slot = slots[p_slot];
	for (int i = 0; i < slot.local_param_count; i++) {
		if (slot.local_param_names[i] == p_name) {
			return slot.local_param_values[i];
		}
	}
	return 0.0f;
}

bool SymphonyVoicePool::has_local_parameter(int p_slot, const StringName &p_name) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, false);
	const VoiceSlot &slot = slots[p_slot];
	for (int i = 0; i < slot.local_param_count; i++) {
		if (slot.local_param_names[i] == p_name) {
			return true;
		}
	}
	return false;
}

void SymphonyVoicePool::clear_local_parameters(int p_slot) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].local_param_count = 0;
}

// --- Importance-Based Mixing ---

constexpr float SymphonyVoicePool::CATEGORY_WEIGHTS[5];

void SymphonyVoicePool::set_listener_position(const Vector3 &p_pos) {
	listener_position = p_pos;
}

Vector3 SymphonyVoicePool::get_listener_position() const {
	return listener_position;
}

void SymphonyVoicePool::set_reference_distance(float p_dist) {
	ref_distance = MAX(p_dist, 1.0f);
}

float SymphonyVoicePool::get_reference_distance() const {
	return ref_distance;
}

void SymphonyVoicePool::set_slot_category(int p_slot, int p_category) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].category = CLAMP(p_category, 0, 4);
}

void SymphonyVoicePool::set_slot_importance_weight(int p_slot, float p_weight) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].importance_weight = MAX(p_weight, 0.0f);
}

void SymphonyVoicePool::set_slot_position(int p_slot, const Vector3 &p_pos) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].position = p_pos;
}

float SymphonyVoicePool::get_slot_importance(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, 0.0f);
	return slots[p_slot].importance;
}

// --- Distance Attenuation ---

void SymphonyVoicePool::set_slot_spatial_mode(int p_slot, int p_mode) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].spatial_mode = CLAMP(p_mode, 0, 2);
}

void SymphonyVoicePool::set_slot_attenuation_model(int p_slot, int p_model) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].attenuation_model = CLAMP(p_model, 0, 5);
}

void SymphonyVoicePool::set_slot_max_distance(int p_slot, float p_distance) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].max_distance = MAX(p_distance, 1.0f);
}

void SymphonyVoicePool::set_slot_inner_radius(int p_slot, float p_radius) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].inner_radius = MAX(p_radius, 0.0f);
}

void SymphonyVoicePool::set_slot_falloff_distance(int p_slot, float p_distance) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].falloff_distance = MAX(p_distance, 0.0f);
}

void SymphonyVoicePool::set_slot_virtualize_when_inaudible(int p_slot, bool p_virtualize) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].virtualize_when_inaudible = p_virtualize;
}

float SymphonyVoicePool::get_slot_attenuation_volume(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, 1.0f);
	return slots[p_slot].attenuation_volume;
}

void SymphonyVoicePool::set_slot_attenuation_curve(int p_slot, const Ref<Curve> &p_curve) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slot_attenuation_curves[p_slot] = p_curve;
}

Ref<Curve> SymphonyVoicePool::get_slot_attenuation_curve(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, Ref<Curve>());
	return slot_attenuation_curves[p_slot];
}

Vector3 SymphonyVoicePool::get_slot_position(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, Vector3());
	return slots[p_slot].position;
}

float SymphonyVoicePool::get_slot_attenuation(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, 1.0f);
	return slots[p_slot].attenuation_volume;
}

float SymphonyVoicePool::get_slot_max_distance(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, 0.0f);
	return slots[p_slot].max_distance;
}

bool SymphonyVoicePool::is_slot_audible(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, false);
	const VoiceSlot &s = slots[p_slot];
	switch (s.state) {
		case VOICE_FREE:
		case VOICE_VIRTUAL:
		case VOICE_VIRTUALIZING:
		case VOICE_STOPPED:
			return false;
		default:
			break;
	}
	return s.attenuation_volume > 0.0001f;
}

void SymphonyVoicePool::set_slot_start_delay(int p_slot, float p_delay_s) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].pending_start_delay_s = MAX(p_delay_s, 0.0f);
}

float SymphonyVoicePool::get_slot_start_delay(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, 0.0f);
	return slots[p_slot].pending_start_delay_s;
}

bool SymphonyVoicePool::is_slot_start_pending(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, pool_size, false);
	return slots[p_slot].state == VOICE_TO_PLAY && slots[p_slot].pending_start_delay_s > 0.0f;
}

void SymphonyVoicePool::tick_deferred_starts(float p_delta) {
	if (p_delta <= 0.0f) {
		return;
	}
	for (int i = 0; i < pool_size; i++) {
		if (slots[i].state == VOICE_TO_PLAY && slots[i].pending_start_delay_s > 0.0f) {
			slots[i].pending_start_delay_s -= p_delta;
			if (slots[i].pending_start_delay_s < 0.0f) {
				slots[i].pending_start_delay_s = 0.0f;
			}
		}
	}
}

void SymphonyVoicePool::update_importance() {
	// Staggered update: process 1/4 of the pool each frame
	int batch_size = (pool_size + IMPORTANCE_UPDATE_INTERVAL - 1) / IMPORTANCE_UPDATE_INTERVAL;
	int start = (importance_update_frame % IMPORTANCE_UPDATE_INTERVAL) * batch_size;
	int count = MIN(batch_size, pool_size - start);

	_update_importance_batch(start, count);
	importance_update_frame++;
}

void SymphonyVoicePool::update_importance_all() {
	// Process the entire pool at once. Use for tests and debug tools only —
	// in production, the staggered update_importance() spreads the cost over
	// IMPORTANCE_UPDATE_INTERVAL frames (currently 4).
	_update_importance_batch(0, pool_size);
}

void SymphonyVoicePool::_update_importance_batch(int p_start, int p_count) {
	float ref_dist_sq = ref_distance * ref_distance;

	for (int i = p_start; i < p_start + p_count && i < pool_size; i++) {
		if (slots[i].state == VOICE_FREE || slots[i].state == VOICE_STOPPED) {
			slots[i].importance = 0.0f;
			slots[i].attenuation_volume = 0.0f;
			continue;
		}

		// distance_factor = 1.0 / (1.0 + distance_sq / ref_distance²)
		float distance_sq = slots[i].position.distance_squared_to(listener_position);
		float distance_factor = 1.0f / (1.0f + distance_sq / ref_dist_sq);

		// category_weight from lookup table
		int cat = CLAMP(slots[i].category, 0, 4);
		float category_weight = CATEGORY_WEIGHTS[cat];

		// importance = priority × distance_factor × importance_weight × category_weight
		slots[i].importance = (float)slots[i].priority * distance_factor * slots[i].importance_weight * category_weight;

		// --- Distance attenuation ---
		// Only compute for spatial voices (2D or 3D)
		if (slots[i].spatial_mode == 0) { // NonPositional
			slots[i].attenuation_volume = 1.0f;
			continue;
		}

		float max_dist = MAX(slots[i].max_distance, 1.0f);
		float distance = Math::sqrt(distance_sq);
		float normalized_distance = CLAMP(distance / max_dist, 0.0f, 1.0f);

		float alpha;
		float effective_max;
		if (slots[i].inner_radius > 0.0f || slots[i].falloff_distance > 0.0f) {
			effective_max = slots[i].inner_radius + (slots[i].falloff_distance > 0.0f ? slots[i].falloff_distance : max_dist - slots[i].inner_radius);
			if (distance <= slots[i].inner_radius) {
				slots[i].attenuation_volume = 1.0f;
				// Still check virtualization with effective_max
				if (distance >= effective_max && slots[i].virtualize_when_inaudible) {
					if (slots[i].state == VOICE_PLAYING) {
						_log_event(StringName(), EVENT_VIRTUALIZED, i, slots[i].importance, StringName("distance_exceeded"));
						slots[i].state = VOICE_VIRTUALIZING;
						slots[i].fade_progress = 1.0f;
						slots[i].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
					}
				}
				continue;
			}
			if (distance >= effective_max) {
				alpha = 1.0f;
			} else {
				alpha = (distance - slots[i].inner_radius) / (effective_max - slots[i].inner_radius);
			}
		} else {
			effective_max = max_dist;
			alpha = normalized_distance; // Same as before
		}

		float attenuation = 1.0f;
		switch (slots[i].attenuation_model) {
			case 0: // Linear
				attenuation = 1.0f - alpha;
				break;
			case 1: // Logarithmic
				attenuation = 1.0f - (Math::log(alpha * 9.0f + 1.0f) / Math::log(10.0f));
				break;
			case 2: { // Custom curve
				Ref<Curve> curve = slot_attenuation_curves[i];
				if (curve.is_valid()) {
					attenuation = CLAMP(curve->sample(alpha), 0.0f, 1.0f);
				} else {
					attenuation = 1.0f - alpha;
				}
			} break;
			case 3: // Natural
				attenuation = Math::pow(1.0f - alpha, 1.5f);
				break;
			case 4: // Log Reverse
				attenuation = Math::log(1.0f + (1.0f - alpha) * 9.0f) / Math::log(10.0f);
				break;
			case 5: // Inverse Square
				attenuation = 1.0f / (1.0f + alpha * 9.0f);
				break;
		}

		slots[i].attenuation_volume = attenuation;

		// Auto-virtualize if beyond effective_max and virtualize_when_inaudible is set
		if (distance >= effective_max && slots[i].virtualize_when_inaudible) {
			if (slots[i].state == VOICE_PLAYING) {
				_log_event(StringName(), EVENT_VIRTUALIZED, i, slots[i].importance, StringName("distance_exceeded"));
				slots[i].state = VOICE_VIRTUALIZING;
				slots[i].fade_progress = 1.0f;
				slots[i].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
			}
		} else if (slots[i].state == VOICE_VIRTUAL && distance < effective_max * 0.95f) {
			// Devirtualize when listener comes back within 95% of effective_max (hysteresis)
			_log_event(StringName(), EVENT_DEVIRTUALIZED, i, slots[i].importance, StringName("listener_returned"));
			slots[i].state = VOICE_DEVIRTUALIZING;
			slots[i].fade_progress = 0.0f;
			slots[i].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
		}
	}
}

// --- Per-Category Voice Counts ---

Dictionary SymphonyVoicePool::get_category_voice_counts() const {
	int counts[5] = {0, 0, 0, 0, 0}; // SFX, Music, UI, Ambient, Voice
	for (int i = 0; i < pool_size; i++) {
		if (slots[i].state == VOICE_PLAYING || slots[i].state == VOICE_TO_PLAY ||
				slots[i].state == VOICE_VIRTUALIZING || slots[i].state == VOICE_DEVIRTUALIZING) {
			int cat = CLAMP(slots[i].category, 0, 4);
			counts[cat]++;
		}
	}
	Dictionary d;
	d["sfx"] = counts[0];
	d["music"] = counts[1];
	d["ui"] = counts[2];
	d["ambient"] = counts[3];
	d["voice"] = counts[4];
	return d;
}

// --- LOD Control ---

void SymphonyVoicePool::force_lod(int p_slot, int p_lod_tier) {
	if (p_slot < 0 || p_slot >= pool_size) {
		return;
	}
	slots[p_slot].target_lod = CLAMP(p_lod_tier, 0, 2);
	slots[p_slot].current_lod = slots[p_slot].target_lod;
	slots[p_slot].lod_forced = true;
}

void SymphonyVoicePool::release_lod_force(int p_slot) {
	if (p_slot < 0 || p_slot >= pool_size) {
		return;
	}
	slots[p_slot].lod_forced = false;
}

int SymphonyVoicePool::get_slot_current_lod(int p_slot) const {
	if (p_slot < 0 || p_slot >= pool_size) {
		return 0;
	}
	return slots[p_slot].current_lod;
}

int SymphonyVoicePool::get_slot_target_lod(int p_slot) const {
	if (p_slot < 0 || p_slot >= pool_size) {
		return 0;
	}
	return slots[p_slot].target_lod;
}

void SymphonyVoicePool::update_lod_targets() {
	// Compute LOD targets based on distance ratio for each active slot.
	// This is called from process_frame() alongside importance updates.
	for (int i = 0; i < pool_size; i++) {
		if (slots[i].state != VOICE_PLAYING && slots[i].state != VOICE_TO_PLAY) {
			continue;
		}
		if (slots[i].lod_forced) {
			continue; // Respect forced LOD
		}
		if (slots[i].max_distance <= 0.0f) {
			slots[i].target_lod = 0;
			continue;
		}

		// Compute distance ratio (0 = at listener, 1 = at max_distance)
		float dist_sq = slots[i].position.distance_squared_to(listener_position);
		float max_dist_sq = slots[i].max_distance * slots[i].max_distance;
		float distance_ratio = (max_dist_sq > 0.0f) ? Math::sqrt(dist_sq / max_dist_sq) : 0.0f;
		distance_ratio = CLAMP(distance_ratio, 0.0f, 1.0f);

		// Determine target LOD from distance ratio using per-slot thresholds (dev-log #12).
		float t1 = slots[i].lod_threshold_1;
		float t2 = slots[i].lod_threshold_2;
		int new_target;
		if (distance_ratio >= t2) {
			new_target = 2;
		} else if (distance_ratio >= t1) {
			new_target = 1;
		} else {
			new_target = 0;
		}

		// Hysteresis: only upgrade (lower LOD number) if distance drops below threshold - 5%
		if (new_target < slots[i].current_lod) {
			float hysteresis = 0.05f;
			if (new_target == 0 && distance_ratio > (t1 - hysteresis)) {
				new_target = 1; // Stay at LOD 1
			} else if (new_target == 1 && distance_ratio > (t2 - hysteresis)) {
				new_target = 2; // Stay at LOD 2
			}
		}

		slots[i].target_lod = new_target;
	}
}

void SymphonyVoicePool::set_slot_lod_thresholds(int p_slot, float p_threshold_1, float p_threshold_2) {
	ERR_FAIL_INDEX(p_slot, pool_size);
	slots[p_slot].lod_threshold_1 = CLAMP(p_threshold_1, 0.0f, 1.0f);
	slots[p_slot].lod_threshold_2 = CLAMP(p_threshold_2, 0.0f, 1.0f);
	// Ensure threshold_2 >= threshold_1
	if (slots[p_slot].lod_threshold_2 < slots[p_slot].lod_threshold_1) {
		slots[p_slot].lod_threshold_2 = slots[p_slot].lod_threshold_1;
	}
}

// --- Event Log ---

void SymphonyVoicePool::_log_event(const StringName &p_event_name, EventResult p_result, int p_slot, float p_importance, const StringName &p_steal_reason) {
	AudioEventLog &entry = event_log[event_log_write_index];
	entry.timestamp_usec = OS::get_singleton()->get_ticks_usec();
	entry.event_name = p_event_name;
	entry.result = p_result;
	entry.voice_slot = p_slot;
	entry.importance = p_importance;
	entry.steal_reason = p_steal_reason;

	event_log_write_index = (event_log_write_index + 1) % EVENT_LOG_SIZE;
	if (event_log_count < EVENT_LOG_SIZE) {
		event_log_count++;
	}
}

void SymphonyVoicePool::log_event(const StringName &p_event_name, EventResult p_result, int p_slot, float p_importance, const StringName &p_steal_reason) {
	_log_event(p_event_name, p_result, p_slot, p_importance, p_steal_reason);
}

Array SymphonyVoicePool::get_recent_events(int p_count) const {
	Array result;
	int count = MIN(p_count, event_log_count);
	if (count <= 0) {
		return result;
	}

	// Read from most recent backwards
	int read_index = event_log_write_index - 1;
	if (read_index < 0) {
		read_index = EVENT_LOG_SIZE - 1;
	}

	for (int i = 0; i < count; i++) {
		const AudioEventLog &entry = event_log[read_index];

		Dictionary d;
		d["timestamp_usec"] = entry.timestamp_usec;
		d["event_name"] = entry.event_name;
		d["result"] = (int)entry.result;
		d["voice_slot"] = entry.voice_slot;
		d["importance"] = entry.importance;
		d["steal_reason"] = entry.steal_reason;

		// Human-readable result string for debug overlay
		switch (entry.result) {
			case EVENT_PLAYED: d["result_text"] = "Played"; break;
			case EVENT_STOLEN: d["result_text"] = "Stolen"; break;
			case EVENT_REJECTED_COOLDOWN: d["result_text"] = "Rejected: cooldown"; break;
			case EVENT_REJECTED_VOICE_LIMIT: d["result_text"] = "Rejected: voice limit"; break;
			case EVENT_REJECTED_NO_STREAMS: d["result_text"] = "Rejected: no streams"; break;
			case EVENT_VIRTUALIZED: d["result_text"] = "Virtualized"; break;
			case EVENT_DEVIRTUALIZED: d["result_text"] = "Devirtualized"; break;
		}

		result.append(d);

		read_index--;
		if (read_index < 0) {
			read_index = EVENT_LOG_SIZE - 1;
		}
	}

	return result;
}

void SymphonyVoicePool::process_frame() {
	int active = 0;
	int virtual_count = 0;

	// Compute wall-clock delta since the last process_frame() for time-based
	// countdowns (propagation delay). First call yields 0 (no jump).
	uint64_t now_usec = OS::get_singleton()->get_ticks_usec();
	float delta_s = 0.0f;
	if (last_process_usec != 0 && now_usec > last_process_usec) {
		delta_s = (float)(now_usec - last_process_usec) / 1000000.0f;
	}
	last_process_usec = now_usec;

	// Advance any pending propagation-delay countdowns before the state machine.
	tick_deferred_starts(delta_s);

	for (int i = 0; i < pool_size; i++) {
		switch (slots[i].state) {
			case VOICE_TO_PLAY:
				// Propagation delay: hold the voice until its start delay elapses
				// (deferred one-shot start, no SceneTreeTimer). While pending it
				// counts as an active voice so it isn't reclaimed as "free".
				if (slots[i].pending_start_delay_s > 0.0f) {
					active++;
					break;
				}
				slots[i].state = VOICE_PLAYING;
				slots[i].fade_progress = MIN(slots[i].fade_progress + slots[i].fade_speed * ANTI_CLICK_SAMPLES, 1.0f);
				active++;
				break;
			case VOICE_PLAYING:
				active++;
				break;
			case VOICE_VIRTUALIZING:
				slots[i].fade_progress -= slots[i].fade_speed * ANTI_CLICK_SAMPLES;
				if (slots[i].fade_progress <= 0.0f) {
					slots[i].state = VOICE_VIRTUAL;
					slots[i].fade_progress = 0.0f;
					emit_signal(SNAME("voice_virtualized"), i);
				} else {
					active++;
				}
				break;
			case VOICE_VIRTUAL:
				virtual_count++;
				break;
			case VOICE_DEVIRTUALIZING:
				slots[i].fade_progress += slots[i].fade_speed * ANTI_CLICK_SAMPLES;
				if (slots[i].fade_progress >= 1.0f) {
					slots[i].state = VOICE_PLAYING;
					slots[i].fade_progress = 1.0f;
					emit_signal(SNAME("voice_devirtualized"), i);
				}
				active++;
				break;
			case VOICE_STOPPING:
				slots[i].fade_progress -= slots[i].fade_speed * ANTI_CLICK_SAMPLES;
				if (slots[i].fade_progress <= 0.0f) {
					slots[i].state = VOICE_FREE;
					slots[i].fade_progress = 0.0f;
				} else {
					active++;
				}
				break;
			case VOICE_STOPPED:
				slots[i].state = VOICE_FREE;
				break;
			default:
				break;
		}
	}

	// Update importance (staggered — 1/4 of pool per frame)
	update_importance();

	// Update LOD targets based on distance
	update_lod_targets();

	VoiceMetrics m;
	m.active = active;
	m.virtual_count = virtual_count;
	m.stolen_this_frame = stolen_this_frame;
	m.budget_percent = (pool_size > 0) ? ((float)active / (float)pool_size) * 100.0f : 0.0f;
	metrics.store(m, std::memory_order_relaxed);

	stolen_this_frame = 0;
}
