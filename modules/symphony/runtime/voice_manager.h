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
	};

	struct VoiceMetrics {
		int active = 0;
		int virtual_count = 0;
		int stolen_this_frame = 0;
		float budget_percent = 0.0f;
	};

	static constexpr int ANTI_CLICK_SAMPLES = 64;

private:
	static SymphonyVoicePool *singleton;

	VoiceSlot *slots = nullptr;
	int pool_size = 48;
	std::atomic<VoiceMetrics> metrics;
	int stolen_this_frame = 0;

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

	void process_frame();

	SymphonyVoicePool();
	~SymphonyVoicePool();
};

VARIANT_ENUM_CAST(SymphonyVoicePool::VoiceState);
