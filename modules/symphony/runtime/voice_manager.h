#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/config/project_settings.h"
#include <atomic>

class SoundEvent;

// Game-level voice pool manager. Fixed-size pool with state machine,
// priority-based stealing, virtualization, and lock-free metrics.
// This is the public API for the Game Audio Layer.
class SymphonyVoicePool : public Object {
	GDCLASS(SymphonyVoicePool, Object);

public:
	static constexpr int ANTI_CLICK_SAMPLES = 64;
	static constexpr int MAX_LOCAL_PARAMS = 8;

	enum VoiceState {
		VOICE_FREE = 0,
		VOICE_TO_PLAY,
		VOICE_PLAYING,
		VOICE_VIRTUALIZING,
		VOICE_VIRTUAL,
		VOICE_DEVIRTUALIZING,
		VOICE_STOPPING,
		VOICE_STOPPED,
	};

	struct VoiceSlot {
		VoiceState state = VOICE_FREE;
		uint64_t event_id = 0;
		int priority = 50;
		float importance = 0.0f;
		float rms = 0.0f;
		Vector3 position;
		float fade_progress = 0.0f;
		float fade_speed = 0.0f;
		uint64_t start_time = 0;
		int category = 0; // SoundEvent::Category (0=SFX,1=Music,2=UI,3=Ambient,4=Voice)
		float importance_weight = 1.0f; // Per-event weight from SoundEvent

		// Per-voice local RTPC parameters (override global)
		StringName local_param_names[MAX_LOCAL_PARAMS];
		float local_param_values[MAX_LOCAL_PARAMS];
		int local_param_count = 0;
	};

	struct VoiceMetrics {
		int active = 0;
		int virtual_count = 0;
		int stolen_this_frame = 0;
		float budget_percent = 0.0f;
	};

private:
	static SymphonyVoicePool *singleton;

	VoiceSlot *slots = nullptr;
	int pool_size = 48;
	std::atomic<VoiceMetrics> metrics;
	int stolen_this_frame = 0;

	// Importance computation state
	Vector3 listener_position;
	float ref_distance = 500.0f; // Reference distance for distance_factor calculation
	int importance_update_frame = 0; // Frame counter for staggered updates
	static constexpr int IMPORTANCE_UPDATE_INTERVAL = 4; // Update every N frames
	static constexpr float CATEGORY_WEIGHTS[5] = {1.0f, 1.0f, 1.5f, 0.5f, 2.0f}; // SFX, Music, UI, Ambient, Voice

	void _update_importance_batch(int p_start, int p_count);

protected:
	static void _bind_methods();

public:
	static SymphonyVoicePool *get_singleton() { return singleton; }

	int acquire_slot(int p_priority);
	void release_slot(int p_index, bool p_immediate = false);
	int steal_lowest_importance();
	void virtualize(int p_index);
	void devirtualize(int p_index);

	VoiceState get_slot_state(int p_index) const;
	int get_pool_size() const { return pool_size; }
	VoiceSlot *get_slot(int p_index);
	const VoiceSlot *get_slot(int p_index) const;

	int get_active_voice_count() const;
	int get_virtual_voice_count() const;
	int get_stolen_this_frame() const;
	float get_budget_percent() const;

	// Per-voice local parameters
	void set_local_parameter(int p_slot, const StringName &p_name, float p_value);
	float get_local_parameter(int p_slot, const StringName &p_name) const;
	bool has_local_parameter(int p_slot, const StringName &p_name) const;
	void clear_local_parameters(int p_slot);

	// Importance-based mixing
	void set_listener_position(const Vector3 &p_pos);
	Vector3 get_listener_position() const;
	void set_reference_distance(float p_dist);
	float get_reference_distance() const;
	void set_slot_category(int p_slot, int p_category);
	void set_slot_importance_weight(int p_slot, float p_weight);
	void set_slot_position(int p_slot, const Vector3 &p_pos);
	float get_slot_importance(int p_slot) const;
	void update_importance(); // Called each frame (internally handles staggering)

	void process_frame();

	SymphonyVoicePool();
	~SymphonyVoicePool();
};

VARIANT_ENUM_CAST(SymphonyVoicePool::VoiceState);
