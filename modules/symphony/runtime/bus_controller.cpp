#include "bus_controller.h"
#include "servers/audio/audio_server.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"

BusController *BusController::singleton = nullptr;

BusController::BusController() {
	singleton = this;

	// Default ducking targets
	ducking_config.target_buses.push_back(StringName("Music"));

	// Compute smoothing coefficients (assuming 60fps frame rate)
	// coeff = 1 - exp(-1 / (time_constant_in_frames))
	// time_constant_in_frames = (ms / 1000) * 60
	float attack_frames = (ducking_config.attack_ms / 1000.0f) * 60.0f;
	float release_frames = (ducking_config.release_ms / 1000.0f) * 60.0f;
	ducking_state.smooth_coeff_attack = 1.0f - Math::exp(-1.0f / MAX(attack_frames, 0.001f));
	ducking_state.smooth_coeff_release = 1.0f - Math::exp(-1.0f / MAX(release_frames, 0.001f));

	// Capture default snapshot at startup (deferred — AudioServer may not be ready yet)
	// Will be captured on first process() call if not already present
}

BusController::~BusController() {
	singleton = nullptr;
}

void BusController::_bind_methods() {
	// Snapshot API
	ClassDB::bind_method(D_METHOD("capture_snapshot", "name"), &BusController::capture_snapshot);
	ClassDB::bind_method(D_METHOD("apply_snapshot", "name", "transition_time"), &BusController::apply_snapshot, DEFVAL(0.0f));
	ClassDB::bind_method(D_METHOD("get_snapshot_names"), &BusController::get_snapshot_names);
	ClassDB::bind_method(D_METHOD("has_snapshot", "name"), &BusController::has_snapshot);
	ClassDB::bind_method(D_METHOD("remove_snapshot", "name"), &BusController::remove_snapshot);

	// Ducking configuration
	ClassDB::bind_method(D_METHOD("set_duck_source_bus", "bus"), &BusController::set_duck_source_bus);
	ClassDB::bind_method(D_METHOD("get_duck_source_bus"), &BusController::get_duck_source_bus);
	ClassDB::bind_method(D_METHOD("set_duck_target_buses", "buses"), &BusController::set_duck_target_buses);
	ClassDB::bind_method(D_METHOD("get_duck_target_buses"), &BusController::get_duck_target_buses);
	ClassDB::bind_method(D_METHOD("set_duck_amount_db", "db"), &BusController::set_duck_amount_db);
	ClassDB::bind_method(D_METHOD("get_duck_amount_db"), &BusController::get_duck_amount_db);
	ClassDB::bind_method(D_METHOD("set_duck_attack_ms", "ms"), &BusController::set_duck_attack_ms);
	ClassDB::bind_method(D_METHOD("get_duck_attack_ms"), &BusController::get_duck_attack_ms);
	ClassDB::bind_method(D_METHOD("set_duck_release_ms", "ms"), &BusController::set_duck_release_ms);
	ClassDB::bind_method(D_METHOD("get_duck_release_ms"), &BusController::get_duck_release_ms);
	ClassDB::bind_method(D_METHOD("set_duck_silence_threshold_db", "db"), &BusController::set_duck_silence_threshold_db);
	ClassDB::bind_method(D_METHOD("get_duck_silence_threshold_db"), &BusController::get_duck_silence_threshold_db);

	// Query
	ClassDB::bind_method(D_METHOD("is_ducking_active"), &BusController::is_ducking_active);
	ClassDB::bind_method(D_METHOD("get_current_duck_db"), &BusController::get_current_duck_db);
	ClassDB::bind_method(D_METHOD("is_transition_active"), &BusController::is_transition_active);
	ClassDB::bind_method(D_METHOD("get_transition_progress"), &BusController::get_transition_progress);

	// Process
	ClassDB::bind_method(D_METHOD("process", "delta"), &BusController::process);
}

// --- Snapshot API ---

void BusController::capture_snapshot(const StringName &p_name) {
	AudioServer *as = AudioServer::get_singleton();
	ERR_FAIL_NULL(as);

	BusSnapshot snap;
	snap.name = p_name;

	int bus_count = as->get_bus_count();
	for (int i = 0; i < bus_count; i++) {
		StringName bus_name = as->get_bus_name(i);
		BusState state;
		state.volume_db = as->get_bus_volume_db(i);
		state.muted = as->is_bus_mute(i);
		state.solo = as->is_bus_solo(i);
		snap.bus_states.insert(bus_name, state);
	}

	snapshots.insert(p_name, snap);
}

void BusController::apply_snapshot(const StringName &p_name, float p_transition_time) {
	ERR_FAIL_COND_MSG(!snapshots.has(p_name), vformat("BusController: Snapshot '%s' not found.", String(p_name)));

	const BusSnapshot &target_snap = snapshots[p_name];

	if (p_transition_time <= 0.0f) {
		// Immediate apply
		AudioServer *as = AudioServer::get_singleton();
		ERR_FAIL_NULL(as);

		for (const KeyValue<StringName, BusState> &E : target_snap.bus_states) {
			int idx = as->get_bus_index(E.key);
			if (idx < 0) {
				continue;
			}
			as->set_bus_volume_db(idx, E.value.volume_db);
			as->set_bus_mute(idx, E.value.muted);
			as->set_bus_solo(idx, E.value.solo);
		}
		transition.active = false;
	} else {
		// Start interpolated transition
		AudioServer *as = AudioServer::get_singleton();
		ERR_FAIL_NULL(as);

		// Capture current state as 'from'
		transition.from_state.clear();
		for (const KeyValue<StringName, BusState> &E : target_snap.bus_states) {
			transition.from_state.insert(E.key, _read_current_bus_state(E.key));
		}

		transition.to_state.clear();
		for (const KeyValue<StringName, BusState> &E : target_snap.bus_states) {
			transition.to_state.insert(E.key, E.value);
		}

		transition.progress = 0.0f;
		transition.duration = p_transition_time;
		transition.active = true;
	}
}

PackedStringArray BusController::get_snapshot_names() const {
	PackedStringArray names;
	for (const KeyValue<StringName, BusSnapshot> &E : snapshots) {
		names.push_back(String(E.key));
	}
	return names;
}

bool BusController::has_snapshot(const StringName &p_name) const {
	return snapshots.has(p_name);
}

void BusController::remove_snapshot(const StringName &p_name) {
	snapshots.erase(p_name);
}

// --- Ducking Configuration ---

void BusController::set_duck_source_bus(const StringName &p_bus) {
	ducking_config.source_bus = p_bus;
}

StringName BusController::get_duck_source_bus() const {
	return ducking_config.source_bus;
}

void BusController::set_duck_target_buses(const TypedArray<StringName> &p_buses) {
	ducking_config.target_buses = p_buses;
}

TypedArray<StringName> BusController::get_duck_target_buses() const {
	return ducking_config.target_buses;
}

void BusController::set_duck_amount_db(float p_db) {
	ducking_config.duck_amount_db = p_db;
}

float BusController::get_duck_amount_db() const {
	return ducking_config.duck_amount_db;
}

void BusController::set_duck_attack_ms(float p_ms) {
	ducking_config.attack_ms = p_ms;
	float attack_frames = (p_ms / 1000.0f) * 60.0f;
	ducking_state.smooth_coeff_attack = 1.0f - Math::exp(-1.0f / MAX(attack_frames, 0.001f));
}

float BusController::get_duck_attack_ms() const {
	return ducking_config.attack_ms;
}

void BusController::set_duck_release_ms(float p_ms) {
	ducking_config.release_ms = p_ms;
	float release_frames = (p_ms / 1000.0f) * 60.0f;
	ducking_state.smooth_coeff_release = 1.0f - Math::exp(-1.0f / MAX(release_frames, 0.001f));
}

float BusController::get_duck_release_ms() const {
	return ducking_config.release_ms;
}

void BusController::set_duck_silence_threshold_db(float p_db) {
	ducking_config.silence_threshold_db = p_db;
}

float BusController::get_duck_silence_threshold_db() const {
	return ducking_config.silence_threshold_db;
}

// --- Query ---

bool BusController::is_ducking_active() const {
	return ducking_state.is_ducking;
}

float BusController::get_current_duck_db() const {
	return ducking_state.current_duck_db;
}

bool BusController::is_transition_active() const {
	return transition.active;
}

float BusController::get_transition_progress() const {
	return transition.progress;
}

// --- Process (called each frame) ---

void BusController::process(float p_delta) {
	// Capture default snapshot on first call if not present
	if (!snapshots.has(StringName("default"))) {
		capture_snapshot(StringName("default"));
	}

	_update_transition(p_delta);
	_update_ducking(p_delta);
}

void BusController::_update_transition(float p_delta) {
	if (!transition.active) {
		return;
	}

	transition.progress += p_delta / transition.duration;

	if (transition.progress >= 1.0f) {
		transition.progress = 1.0f;
		transition.active = false;
	}

	// Apply interpolated state
	for (const KeyValue<StringName, BusState> &E : transition.to_state) {
		const StringName &bus_name = E.key;
		const BusState &to = E.value;

		BusState from;
		if (transition.from_state.has(bus_name)) {
			from = transition.from_state[bus_name];
		}

		_apply_bus_state_lerp(bus_name, from, to, transition.progress);
	}
}

void BusController::_update_ducking(float p_delta) {
	AudioServer *as = AudioServer::get_singleton();
	ERR_FAIL_NULL(as);

	// Check source bus peak level
	int source_idx = as->get_bus_index(ducking_config.source_bus);
	if (source_idx < 0) {
		return;
	}

	float peak_left = as->get_bus_peak_volume_left_db(source_idx, 0);
	float peak_right = as->get_bus_peak_volume_right_db(source_idx, 0);
	float peak = MAX(peak_left, peak_right);

	// Determine if ducking should be active
	bool should_duck = (peak > ducking_config.silence_threshold_db);
	ducking_state.is_ducking = should_duck;

	// Set target
	ducking_state.target_duck_db = should_duck ? ducking_config.duck_amount_db : 0.0f;

	// Smooth toward target
	float prev_duck = ducking_state.current_duck_db;
	float coeff = should_duck ? ducking_state.smooth_coeff_attack : ducking_state.smooth_coeff_release;
	ducking_state.current_duck_db += coeff * (ducking_state.target_duck_db - ducking_state.current_duck_db);

	// Snap if close enough
	if (Math::abs(ducking_state.current_duck_db - ducking_state.target_duck_db) < 0.01f) {
		ducking_state.current_duck_db = ducking_state.target_duck_db;
	}

	// Apply duck offset delta to target buses
	float duck_delta = ducking_state.current_duck_db - prev_duck;
	if (Math::abs(duck_delta) > 0.001f) {
		for (int i = 0; i < ducking_config.target_buses.size(); i++) {
			StringName target_bus = ducking_config.target_buses[i];
			int target_idx = as->get_bus_index(target_bus);
			if (target_idx < 0) {
				continue;
			}

			// Track total applied ducking offset per bus
			float prev_applied = 0.0f;
			if (applied_duck_offsets.has(target_bus)) {
				prev_applied = applied_duck_offsets[target_bus];
			}
			float new_applied = CLAMP(prev_applied + duck_delta, -24.0f, 0.0f);
			float actual_delta = new_applied - prev_applied;
			applied_duck_offsets.insert(target_bus, new_applied);

			if (Math::abs(actual_delta) > 0.001f) {
				float current_vol = as->get_bus_volume_db(target_idx);
				as->set_bus_volume_db(target_idx, current_vol + actual_delta);
			}
		}
	}
}

void BusController::_apply_bus_state_lerp(const StringName &p_bus, const BusState &p_from, const BusState &p_to, float p_t) {
	AudioServer *as = AudioServer::get_singleton();
	ERR_FAIL_NULL(as);

	int idx = as->get_bus_index(p_bus);
	if (idx < 0) {
		return;
	}

	// Linear interpolation in dB space
	float vol = Math::lerp(p_from.volume_db, p_to.volume_db, p_t);
	as->set_bus_volume_db(idx, vol);

	// Mute/solo: switch at 50% progress
	if (p_t >= 0.5f) {
		as->set_bus_mute(idx, p_to.muted);
		as->set_bus_solo(idx, p_to.solo);
	} else {
		as->set_bus_mute(idx, p_from.muted);
		as->set_bus_solo(idx, p_from.solo);
	}
}

BusController::BusState BusController::_read_current_bus_state(const StringName &p_bus) const {
	BusState state;
	AudioServer *as = AudioServer::get_singleton();
	if (!as) {
		return state;
	}

	int idx = as->get_bus_index(p_bus);
	if (idx < 0) {
		return state;
	}

	state.volume_db = as->get_bus_volume_db(idx);
	state.muted = as->is_bus_mute(idx);
	state.solo = as->is_bus_solo(idx);
	return state;
}
