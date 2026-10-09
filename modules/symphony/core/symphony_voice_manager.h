#pragma once

#include "core/object/object.h"
#include "servers/audio/audio_server.h"
#include "core/object/class_db.h"
#include "core/os/mutex.h"
#include "core/templates/vector.h"
#include "core/templates/safe_list.h"
#include "core/variant/dictionary.h"
#include "../stream/audio_stream_playback_symphony.h"
#include <atomic>
#include <cstdint>

// Singleton that tracks all active Symphony voices and provides global metrics.
// Lock-free design:
// - active_voices uses SafeList (lock-free linked list) for registration/iteration.
// - Metrics published via atomics (audio thread writes, main thread reads).
// - Audio mix callback only snapshots + writes per-voice atomics (plan §6).
// - LOD compile, stop(), ObjectDB, and SafeList cleanup run on the main thread.
class SymphonyVoiceManager : public Object {
	GDCLASS(SymphonyVoiceManager, Object)

	static SymphonyVoiceManager *singleton;

	// Lock-free voice list: main thread inserts, audio thread iterates. voice_lifetime_mutex ONLY
	// protects erasing from active_voices against an in-flight enforce_voice_limits() iteration;
	// enforce only try_locks it and skips the callback when main is erasing.
	SafeList<AudioStreamPlaybackSymphony *> active_voices;
	// Main-thread owned: one Ref per registered voice. refcount==1 means only we hold it (reapable).
	Vector<Ref<AudioStreamPlaybackSymphony>> retained_voices;
	BinaryMutex voice_lifetime_mutex;

	std::atomic<int32_t> max_voices{ 0 }; // 0 = unlimited
	std::atomic<float> warning_threshold{ 0.70f };
	std::atomic<float> critical_threshold{ 0.90f };

	// LOD hysteresis (plan P1 §V). Percent of the block deadline; R = W - release_hysteresis.
	static constexpr float RELEASE_HYSTERESIS = 0.15f;
	static constexpr float ENGAGE_TARGET_BELOW_WARNING = 0.05f;
	static constexpr uint64_t WARN_SUSTAIN_US = 30000;
	static constexpr uint64_t FLOOR_CHANGE_MIN_US = 150000;
	static constexpr uint64_t RELEASE_QUIET_US = 1500000;
	static constexpr uint64_t RELEASE_SPACING_US = 300000;
	static constexpr uint64_t RELEASE_DWELL_US = 1000000;
	// Decision state: only touched inside enforce_voice_limits() while voice_lifetime_mutex is held.
	bool over_warn_active = false;
	uint64_t over_warn_since_us = 0;
	bool below_release_active = false;
	uint64_t below_release_since_us = 0;
	bool has_released = false;
	uint64_t last_release_us = 0;

	std::atomic<uint64_t> callback_seq{ 0 };
	std::atomic<uint64_t> metric_enforce_skipped{ 0 };

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
	bool mix_callback_registered = false;
	AudioServer *mix_callback_server = nullptr;

	static void _mix_callback(void *p_userdata);
	static void _update_callback(void *p_userdata);
	void _ensure_mix_callback();

protected:
	static void _bind_methods();

public:
	static SymphonyVoiceManager *get_singleton() { return singleton; }

	// Returns true when the voice is tracked. Editor UI sessions do not track voices.
	bool register_voice(AudioStreamPlaybackSymphony *p_voice);
	// Main thread: number of voices the manager still retains (playing or parked).
	[[nodiscard]] int32_t get_retained_voice_count() const { return retained_voices.size(); }
	// Main thread: stopped voices that still hold packages (arena memory) because a Ref is alive.
	[[nodiscard]] int32_t get_parked_package_count() const;
	[[nodiscard]] uint64_t get_callback_seq() const { return callback_seq.load(std::memory_order_acquire); }
	[[nodiscard]] uint64_t get_enforce_skipped_count() const { return metric_enforce_skipped.load(std::memory_order_relaxed); }
	[[nodiscard]] bool is_mix_callback_registered() const { return mix_callback_registered; }

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
	[[nodiscard]] uint32_t get_rt_violation_count() const;
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

	// Audio thread: snapshot + write per-voice atomics only. Never blocks on the voice list lock.
	void enforce_voice_limits();
	// Same with an injected clock (tests).
	void _enforce_voice_limits_at(uint64_t p_now_us);
	// Test hooks: hold the voice list lock as main would while erasing.
	[[nodiscard]] bool _test_try_lock_voice_list() { return voice_lifetime_mutex.try_lock(); }
	void _test_unlock_voice_list() { voice_lifetime_mutex.unlock(); }

	// Main thread: apply stop/LOD requests, SafeList cleanup.
	// Also invoked automatically from AudioServer update callback.
	void process_deferred_lod();

	SymphonyVoiceManager();
	~SymphonyVoiceManager();
};
