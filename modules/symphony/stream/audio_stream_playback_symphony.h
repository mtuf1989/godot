#pragma once

#include "scene/resources/audio/audio_stream.h"
#include "audio_stream_symphony.h"
#include "../core/symphony_pin_types.h"
#include "../core/symphony_compiled_graph.h"
#include "../core/symphony_prepared_graph_package.h"
#include "../nodes/io/symphony_graph_output.h"
#include "../nodes/io/symphony_graph_input.h"
#include "../nodes/io/symphony_trigger_input.h"

#include <atomic>

class AudioStreamPlaybackSymphony : public AudioStreamPlaybackResampled {
	GDCLASS(AudioStreamPlaybackSymphony, AudioStreamPlaybackResampled)
	friend class AudioStreamSymphony;
	friend class SymphonyVoiceManager;

private:
	Ref<AudioStreamSymphony> stream;
	std::atomic<bool> active{ false };
	bool registered_with_manager = false;
	std::atomic<bool> stop_pending{ false };

	// Live / pending packages (plan §4). Superseded packages go to GraphPackageRetirement.
	PreparedGraphPackage *current_package = nullptr;
	std::atomic<PreparedGraphPackage *> pending_package{ nullptr };
	std::atomic<bool> pending_is_lod{ false };
	// Main-thread control target (mirrors current). Audio publishes; main load-check-acts.
	std::atomic<PreparedGraphPackage *> control_package{ nullptr };

	// Transition state (plan §5). At most current + outgoing (+ held incoming for fallback).
	enum class TransitionMode : uint8_t {
		Idle,
		EqualPowerCrossfade, // dual-graph 40 ms
		FallbackFadeOut, // single-graph fade out over 64 samples
		FallbackFadeIn, // single-graph fade in over 64 samples
	};
	TransitionMode transition_mode = TransitionMode::Idle;
	PreparedGraphPackage *outgoing_package = nullptr;
	PreparedGraphPackage *incoming_package = nullptr; // held during FallbackFadeOut
	float transition_progress = 1.0f;
	float transition_speed = 0.0f;
	bool holds_crossfade_token = false;
	int current_lod_tier = 0;

	static constexpr float CROSSFADE_SECONDS = 0.040f;
	static constexpr int FALLBACK_FADE_SAMPLES = 64;

	// Convenience mirrors of current_package (audio-thread only for writes).
	CompiledGraph *current_graph = nullptr;
	SymphonyGraphOutput *graph_output_node = nullptr;

	float last_mix_time_us = 0.0f;
	float last_rms = 0.0f;
	int32_t last_frame_count = 0;
	int32_t last_output_frames = 0;
	float mix_rate_cached = 44100.0f;
	float output_rate_cached = 44100.0f;
	// Monotonic authored time in microseconds: rendered output frames × effective speed.
	std::atomic<uint64_t> position_us{ 0 };
	double cached_duration_limit = 0.0;
	std::atomic<bool> unsupported_seek{ false };
	std::atomic<bool> release_pending{ false };
	std::atomic<int32_t> release_grace{ 0 };
	std::atomic<bool> release_ready{ false };
	static constexpr int32_t RELEASE_GRACE_CALLBACKS = 2;

	// Cached Resource-derived fields for audio-thread reads (plan §6).
	int cached_priority = 50;
	int cached_max_lod = 0;

	// Manager → main-thread requests (audio writes atomics only).
	std::atomic<int32_t> requested_lod_tier{ -1 };
	std::atomic<int32_t> budget_lod_floor{ 0 };
	std::atomic<bool> manager_stop_request{ false };

	void _install_package(PreparedGraphPackage *p_package);
	void _finalize_stop();
	void _register_with_manager();
	void _arm_release_grace();
	void _report_unsupported_seek();
	[[nodiscard]] bool _source_or_graph_finished() const;
	void _release_crossfade_token();
	void _abort_transition_packages();
	enum class AdmitResult : uint8_t { Denied, AdmittedNoToken, AdmittedWithToken };
	[[nodiscard]] AdmitResult _try_admit_crossfade(const PreparedGraphPackage *p_incoming);
	void _begin_equal_power_crossfade(PreparedGraphPackage *p_new_package);
	void _begin_fallback_transition(PreparedGraphPackage *p_new_package);
	void _migrate_into(PreparedGraphPackage *p_destination);
	void _cache_stream_metadata();

protected:
	static void _bind_methods();
	virtual int _mix_internal(AudioFrame *p_buffer, int p_frames) override;
	virtual float get_stream_sampling_rate() override;

public:
	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual bool is_playing() const override;
	virtual int get_loop_count() const override;
	virtual double get_playback_position() const override;
	virtual void seek(double p_time) override;
	virtual int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;

	// Hot-swap: wraps the compiled graph in a PreparedGraphPackage (main thread).
	void swap_graph(CompiledGraph *p_graph);

	void transition_to_lod(int p_lod_tier);
	[[nodiscard]] int get_current_lod_tier() const { return current_lod_tier; }
	[[nodiscard]] bool is_lod_transitioning() const {
		return transition_mode != TransitionMode::Idle || requested_lod_tier.load(std::memory_order_relaxed) >= 0;
	}

	// Audio-thread-safe request writers (SymphonyVoiceManager).
	void request_lod_tier(int32_t p_lod_tier);
	void set_budget_lod_floor(int32_t p_lod_tier);
	[[nodiscard]] int get_budget_lod_floor() const { return budget_lod_floor.load(std::memory_order_acquire); }
	void request_manager_stop();
	// Main thread: apply pending manager requests for this voice.
	void process_manager_requests();

	[[nodiscard]] int get_cached_max_lod() const { return cached_max_lod; }

	virtual void set_parameter(const StringName &p_name, const Variant &p_value) override;
	virtual Variant get_parameter(const StringName &p_name) const override;
	bool trigger(const StringName &p_name, float p_value = 1.0f);

	[[nodiscard]] float get_voice_cpu_microseconds() const;
	[[nodiscard]] float get_budget_percent() const;
	[[nodiscard]] float get_last_rms() const;
	[[nodiscard]] int get_effective_priority() const;
	[[nodiscard]] float get_estimated_cost_units() const;

	// Test/debug: push WavePlayer finished on the live package and mix once.
	bool fire_source_finished_and_mix(AudioFrame *p_buffer, int p_frames);

	// Audio thread: count down the post-completion grace. Returns true once the
	// voice must no longer be dereferenced by the manager callback.
	bool tick_release_grace();
	[[nodiscard]] bool is_release_ready() const { return release_ready.load(std::memory_order_acquire); }
	[[nodiscard]] bool is_registered_with_voice_manager() const { return registered_with_manager; }
	[[nodiscard]] bool has_unsupported_seek() const { return unsupported_seek.load(std::memory_order_acquire); }

	~AudioStreamPlaybackSymphony();
};
