#include "voice_manager.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

#include <cfloat>

SymphonyVoicePool *SymphonyVoicePool::singleton = nullptr;

SymphonyVoicePool::SymphonyVoicePool() {
	singleton = this;
	pool_size = GLOBAL_DEF("audio/symphony/voice_pool_size", 48);
	ProjectSettings::get_singleton()->set_custom_property_info(PropertyInfo(
			Variant::INT, "audio/symphony/voice_pool_size", PROPERTY_HINT_RANGE, "8,128,1"));
	slots = memnew_arr(VoiceSlot, pool_size);
	VoiceMetrics m;
	metrics.store(m, std::memory_order_relaxed);
}

SymphonyVoicePool::~SymphonyVoicePool() {
	if (slots) {
		memdelete_arr(slots);
		slots = nullptr;
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

	BIND_ENUM_CONSTANT(VOICE_FREE);
	BIND_ENUM_CONSTANT(VOICE_TO_PLAY);
	BIND_ENUM_CONSTANT(VOICE_PLAYING);
	BIND_ENUM_CONSTANT(VOICE_VIRTUALIZING);
	BIND_ENUM_CONSTANT(VOICE_VIRTUAL);
	BIND_ENUM_CONSTANT(VOICE_DEVIRTUALIZING);
	BIND_ENUM_CONSTANT(VOICE_STOPPING);
	BIND_ENUM_CONSTANT(VOICE_STOPPED);
}

int SymphonyVoicePool::acquire_slot(int p_priority) {
	for (int i = 0; i < pool_size; i++) {
		if (slots[i].state == VOICE_FREE) {
			slots[i].state = VOICE_TO_PLAY;
			slots[i].priority = p_priority;
			slots[i].importance = (float)p_priority;
			slots[i].rms = 0.0f;
			slots[i].fade_progress = 0.0f;
			slots[i].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
			slots[i].start_time = OS::get_singleton()->get_ticks_usec();
			slots[i].local_param_count = 0;
			return i;
		}
	}
	int stolen = steal_lowest_importance();
	if (stolen >= 0) {
		slots[stolen].state = VOICE_TO_PLAY;
		slots[stolen].priority = p_priority;
		slots[stolen].importance = (float)p_priority;
		slots[stolen].rms = 0.0f;
		slots[stolen].fade_progress = 0.0f;
		slots[stolen].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
		slots[stolen].start_time = OS::get_singleton()->get_ticks_usec();
		slots[stolen].local_param_count = 0;
	}
	return stolen;
}

void SymphonyVoicePool::release_slot(int p_index, bool p_immediate) {
	ERR_FAIL_INDEX(p_index, pool_size);
	if (p_immediate) {
		slots[p_index].state = VOICE_FREE;
	} else {
		slots[p_index].state = VOICE_STOPPING;
		slots[p_index].fade_progress = 1.0f;
		slots[p_index].fade_speed = 1.0f / ANTI_CLICK_SAMPLES;
	}
}

int SymphonyVoicePool::steal_lowest_importance() {
	int worst_idx = -1;
	float worst_importance = FLT_MAX;

	for (int i = 0; i < pool_size; i++) {
		if (slots[i].state == VOICE_PLAYING || slots[i].state == VOICE_TO_PLAY) {
			if (slots[i].importance < worst_importance) {
				worst_importance = slots[i].importance;
				worst_idx = i;
			}
		}
	}

	if (worst_idx >= 0) {
		slots[worst_idx].state = VOICE_FREE;
		stolen_this_frame++;
	}
	return worst_idx;
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

void SymphonyVoicePool::update_importance() {
	// Staggered update: process 1/4 of the pool each frame
	int batch_size = (pool_size + IMPORTANCE_UPDATE_INTERVAL - 1) / IMPORTANCE_UPDATE_INTERVAL;
	int start = (importance_update_frame % IMPORTANCE_UPDATE_INTERVAL) * batch_size;
	int count = MIN(batch_size, pool_size - start);

	_update_importance_batch(start, count);
	importance_update_frame++;
}

void SymphonyVoicePool::_update_importance_batch(int p_start, int p_count) {
	float ref_dist_sq = ref_distance * ref_distance;

	for (int i = p_start; i < p_start + p_count && i < pool_size; i++) {
		if (slots[i].state == VOICE_FREE || slots[i].state == VOICE_STOPPED) {
			slots[i].importance = 0.0f;
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
	}
}

void SymphonyVoicePool::process_frame() {
	int active = 0;
	int virtual_count = 0;

	for (int i = 0; i < pool_size; i++) {
		switch (slots[i].state) {
			case VOICE_TO_PLAY:
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

	VoiceMetrics m;
	m.active = active;
	m.virtual_count = virtual_count;
	m.stolen_this_frame = stolen_this_frame;
	m.budget_percent = (pool_size > 0) ? ((float)active / (float)pool_size) * 100.0f : 0.0f;
	metrics.store(m, std::memory_order_relaxed);

	stolen_this_frame = 0;
}
