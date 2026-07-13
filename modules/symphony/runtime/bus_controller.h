#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

/// BusController manages audio bus snapshots and auto-ducking.
///
/// @warning Ducking and external category volume changes (e.g. set_category_volume
/// in the Game Audio Layer) both modify the same AudioServer bus volume_db.
/// Their effects stack additively. For example, if auto-ducking applies -6 dB
/// and category volume is set to -3 dB, the bus will be at -9 dB relative to
/// its snapshot baseline. This is intentional but may surprise users who expect
/// independent volume lanes. Consider using Godot's AudioServer bus effects or
/// separate buses if independent control is needed.
class BusController : public Object {
	GDCLASS(BusController, Object);

public:
	struct BusState {
		float volume_db = 0.0f;
		bool muted = false;
		bool solo = false;
	};

	struct BusSnapshot {
		StringName name;
		HashMap<StringName, BusState> bus_states;
	};

	// Interpolation state for active transition
	struct TransitionState {
		HashMap<StringName, BusState> from_state;
		HashMap<StringName, BusState> to_state;
		float progress = 0.0f;
		float duration = 0.0f;
		bool active = false;
	};

	// Ducking state
	struct DuckingConfig {
		StringName source_bus = "Voice";
		TypedArray<StringName> target_buses; // initialized in constructor
		float duck_amount_db = -6.0f;
		float attack_ms = 100.0f;
		float release_ms = 500.0f;
		float silence_threshold_db = -60.0f;
	};

	struct DuckingState {
		bool is_ducking = false;
		float current_duck_db = 0.0f; // 0 = no duck, negative = ducked
		float target_duck_db = 0.0f;
		float smooth_coeff_attack = 0.0f;
		float smooth_coeff_release = 0.0f;
		bool warned_about_stacking = false;
	};

private:
	static BusController *singleton;

	HashMap<StringName, BusSnapshot> snapshots;
	TransitionState transition;
	DuckingConfig ducking_config;
	DuckingState ducking_state;

	// Track ducking applied per bus so we can undo it
	HashMap<StringName, float> applied_duck_offsets;

	void _update_transition(float p_delta);
	void _update_ducking(float p_delta);
	void _apply_bus_state_lerp(const StringName &p_bus, const BusState &p_from, const BusState &p_to, float p_t);
	BusState _read_current_bus_state(const StringName &p_bus) const;

protected:
	static void _bind_methods();

public:
	static BusController *get_singleton() { return singleton; }

	// Snapshot API
	void capture_snapshot(const StringName &p_name);
	void apply_snapshot(const StringName &p_name, float p_transition_time = 0.0f);
	PackedStringArray get_snapshot_names() const;
	bool has_snapshot(const StringName &p_name) const;
	void remove_snapshot(const StringName &p_name);

	// Ducking configuration
	void set_duck_source_bus(const StringName &p_bus);
	StringName get_duck_source_bus() const;
	void set_duck_target_buses(const TypedArray<StringName> &p_buses);
	TypedArray<StringName> get_duck_target_buses() const;
	void set_duck_amount_db(float p_db);
	float get_duck_amount_db() const;
	void set_duck_attack_ms(float p_ms);
	float get_duck_attack_ms() const;
	void set_duck_release_ms(float p_ms);
	float get_duck_release_ms() const;
	void set_duck_silence_threshold_db(float p_db);
	float get_duck_silence_threshold_db() const;

	// Query
	bool is_ducking_active() const;
	float get_current_duck_db() const;
	bool is_transition_active() const;
	float get_transition_progress() const;

	// Called each frame from game code (AudioManager._process)
	void process(float p_delta);

	BusController();
	~BusController();
};
