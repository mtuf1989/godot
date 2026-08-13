#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/templates/safe_list.h"
#include "core/variant/dictionary.h"
#include <atomic>
#include <cstdint>

class AudioStreamPlaybackSymphony;

// Singleton that tracks all active Symphony voices and provides global metrics.
// Lock-free design:
// - active_voices uses SafeList (lock-free linked list) for registration/iteration.
// - Metrics published via atomics (audio thread writes, main thread reads).
// - Audio mix callback only snapshots + writes per-voice atomics (plan §6).
// - LOD compile, stop(), ObjectDB, and SafeList cleanup run on the main thread.
class SymphonyVoiceManager : public Object {
	GDCLASS(SymphonyVoiceManager, Object)

	static SymphonyVoiceManager *singleton;

	// Lock-free voice list: main thread inserts/erases, audio thread iterates.
	SafeList<AudioStreamPlaybackSymphony *> active_voices;

	int32_t max_voices = 0; // 0 = unlimited
	float warning_threshold = 0.70f;
	float critical_threshold = 0.90f;

#if defined(__EMSCRIPTEN__) || defined(ANDROID_ENABLED) || defined(IOS_ENABLED)
	static constexpr int32_t DEFAULT_CROSSFADE_TOKENS = 1;
#else
	static constexpr int32_t DEFAULT_CROSSFADE_TOKENS = 2;
#endif
	std::atomic<int32_t> crossfade_tokens{ DEFAULT_CROSSFADE_TOKENS };

	// --- Lock-free metrics (written by audio thread, read by main thread) ---
	std::atomic<int32_t> metric_active_count{ 0 };
	std::atomic<int32_t> metric_stolen_this_frame{ 0 };
	// Budget metrics stored as fixed-point (value * 1000) to avoid atomic<float> portability issues.
	std::atomic<int32_t> metric_total_budget_millipercent{ 0 };
	std::atomic<int32_t> metric_peak_budget_millipercent{ 0 };
	std::atomic<int32_t> metric_avg_voice_microseconds_x1000{ 0 };
	// EWMA of microseconds per cost unit (×1000). Default 50 → 0.05 µs/unit (conservative).
	std::atomic<int32_t> metric_us_per_cost_x1000{ 50 };
	// Transition admission outcomes (audio thread increments).
	std::atomic<uint64_t> metric_crossfade_transitions{ 0 };
	std::atomic<uint64_t> metric_fallback_transitions{ 0 };

	bool update_callback_registered = false;

	static void _mix_callback(void *p_userdata);
	static void _update_callback(void *p_userdata);

protected:
	static void _bind_methods();

public:
	static SymphonyVoiceManager *get_singleton() { return singleton; }

	void register_voice(AudioStreamPlaybackSymphony *p_voice);
	void unregister_voice(AudioStreamPlaybackSymphony *p_voice);

	// Lock-free getters — read atomic metrics snapshot, no mutex.
	[[nodiscard]] int32_t get_active_voice_count() const;
	[[nodiscard]] float get_total_budget_percent() const;
	[[nodiscard]] float get_peak_budget_percent() const;
	[[nodiscard]] float get_average_voice_microseconds() const;
	[[nodiscard]] int32_t get_stolen_this_frame() const;
	[[nodiscard]] uint64_t get_dropped_trigger_count() const;
	[[nodiscard]] uint64_t get_spectral_underflow_count() const;
	[[nodiscard]] uint64_t get_crossfade_transition_count() const;
	[[nodiscard]] uint64_t get_fallback_transition_count() const;
	[[nodiscard]] uint64_t get_packages_destroyed_count() const;
	[[nodiscard]] uint32_t get_retirement_pending_count() const;
	[[nodiscard]] uint32_t get_retirement_peak_pending_count() const;
	// Aggregated read-only diagnostics for editor / GDScript (memory + transitions + triggers).
	[[nodiscard]] Dictionary get_debug_metrics() const;

	void note_crossfade_transition();
	void note_fallback_transition();

	void set_max_voices(int32_t p_max);
	int32_t get_max_voices() const;
	void set_warning_threshold(float p_threshold);
	float get_warning_threshold() const;
	void set_critical_threshold(float p_threshold);
	float get_critical_threshold() const;

	// Budget-aware dual-graph transition tokens (plan §5).
	[[nodiscard]] bool try_acquire_crossfade_token();
	void release_crossfade_token();
	[[nodiscard]] int32_t get_crossfade_tokens_available() const;

	// Calibrated estimate of additional CPU fraction for an incoming package cost.
	[[nodiscard]] float estimate_cpu_fraction_for_cost(float p_cost_units, float p_mix_rate, int p_frames) const;
	void observe_cost_sample(float p_cost_units, float p_mix_us);

	// Audio thread: snapshot + write per-voice atomics only.
	void enforce_voice_limits();

	// Main thread: apply stop/LOD requests, SafeList cleanup.
	// Also invoked automatically from AudioServer update callback.
	void process_deferred_lod();

	SymphonyVoiceManager();
	~SymphonyVoiceManager();
};
