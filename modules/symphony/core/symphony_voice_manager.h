#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/templates/safe_list.h"
#include <atomic>

class AudioStreamPlaybackSymphony;

// Singleton that tracks all active Symphony voices and provides global metrics.
// Lock-free design:
// - active_voices uses SafeList (lock-free linked list) for registration/iteration.
// - Metrics published via atomics (audio thread writes, main thread reads).
// - No mutex anywhere — audio thread never blocks.
class SymphonyVoiceManager : public Object {
	GDCLASS(SymphonyVoiceManager, Object)

	static SymphonyVoiceManager *singleton;

	// Lock-free voice list: main thread inserts/erases, audio thread iterates.
	SafeList<AudioStreamPlaybackSymphony *> active_voices;

	int32_t max_voices = 0; // 0 = unlimited
	float warning_threshold = 0.70f;
	float critical_threshold = 0.90f;

	// --- Lock-free metrics (written by audio thread, read by main thread) ---
	std::atomic<int32_t> metric_active_count{ 0 };
	std::atomic<int32_t> metric_stolen_this_frame{ 0 };
	// Budget metrics stored as fixed-point (value * 1000) to avoid atomic<float> portability issues.
	std::atomic<int32_t> metric_total_budget_millipercent{ 0 };
	std::atomic<int32_t> metric_peak_budget_millipercent{ 0 };
	std::atomic<int32_t> metric_avg_voice_microseconds_x1000{ 0 };

	// Mix callback — called once per audio callback cycle.
	static void _mix_callback(void *p_userdata);

protected:
	static void _bind_methods();

public:
	static SymphonyVoiceManager *get_singleton() { return singleton; }

	void register_voice(AudioStreamPlaybackSymphony *p_voice);
	void unregister_voice(AudioStreamPlaybackSymphony *p_voice);

	// Lock-free getters — read atomic metrics snapshot, no mutex.
	int32_t get_active_voice_count() const;
	float get_total_budget_percent() const;
	float get_peak_budget_percent() const;
	float get_average_voice_microseconds() const;
	int32_t get_stolen_this_frame() const;

	void set_max_voices(int32_t p_max);
	int32_t get_max_voices() const;
	void set_warning_threshold(float p_threshold);
	float get_warning_threshold() const;
	void set_critical_threshold(float p_threshold);
	float get_critical_threshold() const;

	// Called once per audio callback cycle via mix callback.
	void enforce_voice_limits();

	SymphonyVoiceManager();
	~SymphonyVoiceManager();
};
