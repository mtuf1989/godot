#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include <mutex>

class AudioStreamPlaybackSymphony;

// Singleton that tracks all active Symphony voices and provides global metrics.
class SymphonyVoiceManager : public Object {
	GDCLASS(SymphonyVoiceManager, Object)

	static SymphonyVoiceManager *singleton;

	mutable std::mutex registry_mutex;
	Vector<AudioStreamPlaybackSymphony *> active_voices;

	int32_t max_voices = 0; // 0 = unlimited
	float warning_threshold = 0.70f;
	float critical_threshold = 0.90f;

protected:
	static void _bind_methods();

public:
	static SymphonyVoiceManager *get_singleton() { return singleton; }

	void register_voice(AudioStreamPlaybackSymphony *p_voice);
	void unregister_voice(AudioStreamPlaybackSymphony *p_voice);

	int32_t get_active_voice_count() const;
	float get_total_budget_percent() const;
	float get_peak_budget_percent() const;
	float get_average_voice_microseconds() const;

	void set_max_voices(int32_t p_max);
	int32_t get_max_voices() const;
	void set_warning_threshold(float p_threshold);
	float get_warning_threshold() const;
	void set_critical_threshold(float p_threshold);
	float get_critical_threshold() const;

	// Called from audio thread after mix() to enforce voice limits.
	void enforce_voice_limits();

	SymphonyVoiceManager();
	~SymphonyVoiceManager();
};
