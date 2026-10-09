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
	// Main-thread only (set by SymphonyVoiceManager::register_voice / reaper).
	bool registered_with_manager = false;

	// Lifetime model (tech-fix-plan P1): single-owner hand-off.
	// - While reachable by any mixer, ONLY the mixing thread writes current_package, current_graph,
	//   graph_output_node, outgoing_package, incoming_package, transition_* and holds_crossfade_token.
	// - stop() is a pure request (active=false). Packages stay parked so stop-then-start works.
	// - Main reclaims packages only once the voice is unreachable (_reclaim_unreachable()).
	PreparedGraphPackage *current_package = nullptr; // audio-owned
	std::atomic<PreparedGraphPackage *> pending_package{ nullptr };
	// Main-thread control target (mirrors current). Audio publishes; main load-check-acts.
	std::atomic<PreparedGraphPackage *> control_package{ nullptr };

	// Transition state (plan §5). Audio-owned plain fields: At most current + outgoing
	// (+ held incoming for fallback). Never read or written off the mixing thread.
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

	static constexpr float CROSSFADE_SECONDS = 0.040f;
	static constexpr int FALLBACK_FADE_SAMPLES = 64;

	// Convenience mirrors of current_package (audio-owned).
	CompiledGraph *current_graph = nullptr;
	SymphonyGraphOutput *graph_output_node = nullptr;

	// LOD bookkeeping. installed_* is published by audio on install; main_requested_lod is main-only
	// and stops repeated recompiles for a tier that is already published.
	std::atomic<int32_t> installed_lod_tier{ 0 };
	int main_requested_lod = 0;

	// Per-voice stats (audio writes, any thread reads; relaxed).
	std::atomic<float> stat_mix_us_last{ 0.0f };
	std::atomic<float> stat_rms{ 0.0f };
	std::atomic<float> stat_cost_units{ 0.0f };
	// Smoothed per-voice cost as % of the block deadline (asymmetric EWMA, spike-clamped).
	std::atomic<float> stat_cost_pct_ewma{ 0.0f };
	std::atomic<uint64_t> last_mix_seq{ 0 };
	std::atomic<int32_t> last_output_frames{ 0 };
	// Audio-owned: reseed the EWMA from the next sample after a package install.
	bool cost_ewma_reseed = true;
	// Manager-owned (written inside enforce_voice_limits only): when the floor last changed.
	std::atomic<uint64_t> floor_changed_us{ UINT64_MAX };

	// Graph-rate frames actually rendered. get_playback_position() is frames / graph rate.
	// Replaces the truncated microsecond accumulator (it drifted by tens of milliseconds
	// over a few minutes whenever frames/rate was not an integer number of microseconds).
	std::atomic<uint64_t> frames_rendered{ 0 };
	// Drawn once in instantiate_playback and reused for every LOD recompile of this voice.
	// Main thread only (compile never runs on the audio thread).
	uint64_t voice_seed = 0;
	std::atomic<bool> unsupported_seek{ false };

	// start() requests consumed on the mixing thread.
	std::atomic<bool> auto_fire_pending{ false };
	std::atomic<bool> resample_reset_pending{ false };

	// Cached Resource-derived fields for audio-thread reads (plan §6).
	std::atomic<float> mix_rate_cached{ 44100.0f };
	std::atomic<float> output_rate_cached{ 44100.0f };
	std::atomic<double> cached_duration_limit{ 0.0 };
	// Pool/music-authored importance + tier for CPU manager steal ranking (P8a N9).
	std::atomic<float> cached_importance{ 50.0f };
	std::atomic<int32_t> cached_tier{ 1 }; // SoundEvent::TIER_IMPORTANT
	std::atomic<int32_t> cached_max_lod{ 0 };

	// Manager → main-thread requests (audio writes atomics only).
	std::atomic<int32_t> requested_lod_tier{ -1 };
	std::atomic<int32_t> budget_lod_floor{ 0 };

#ifdef DEV_ENABLED
	std::atomic<int32_t> mix_depth{ 0 };
#endif

	void _install_package(PreparedGraphPackage *p_package);
	void _publish_restart_package();
	void _reclaim_unreachable();
	void _register_with_manager();
	void _report_unsupported_seek();
	[[nodiscard]] bool _source_or_graph_finished() const;
	void _release_crossfade_token();
	void _abort_transition_packages();
	enum class AdmitResult : uint8_t { Denied, AdmittedNoToken, AdmittedWithToken };
	[[nodiscard]] AdmitResult _try_admit_crossfade(const PreparedGraphPackage *p_incoming);
	void _begin_equal_power_crossfade(PreparedGraphPackage *p_new_package);
	void _begin_fallback_transition(PreparedGraphPackage *p_new_package);
	void _migrate_into(PreparedGraphPackage *p_destination);
	void _adopt_pending(PreparedGraphPackage *p_pending);
	void _cache_stream_metadata();
	void _update_cost_stats(uint64_t p_mix_us, int p_frames, float p_out_rate);
	void _note_rendered_frames(int p_frames);
	void _sync_voice_lod();

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
	// Graph-rate frame counter. Music transport (P7) schedules against this.
	[[nodiscard]] uint64_t get_frames_rendered() const { return frames_rendered.load(std::memory_order_acquire); }
	// Unresampled graph-rate render. render_offline uses this so the WAV rate,
	// frame count and pitch do not depend on the output device.
	int mix_graph_rate(AudioFrame *p_buffer, int p_frames);
	virtual void seek(double p_time) override;
	virtual int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;

	// Hot-swap: wraps the compiled graph in a PreparedGraphPackage (main thread).
	void swap_graph(CompiledGraph *p_graph);

	void transition_to_lod(int p_lod_tier);
	[[nodiscard]] int get_current_lod_tier() const { return installed_lod_tier.load(std::memory_order_relaxed); }
	[[nodiscard]] bool is_lod_transitioning() const {
		return requested_lod_tier.load(std::memory_order_relaxed) >= 0;
	}

	// Audio-thread-safe request writers (SymphonyVoiceManager).
	void request_lod_tier(int32_t p_lod_tier);
	void set_budget_lod_floor(int32_t p_lod_tier);
	[[nodiscard]] int get_budget_lod_floor() const { return budget_lod_floor.load(std::memory_order_acquire); }
	// Main thread: apply pending manager requests for this voice.
	void process_manager_requests();

	[[nodiscard]] int get_cached_max_lod() const { return cached_max_lod.load(std::memory_order_relaxed); }

	virtual void set_parameter(const StringName &p_name, const Variant &p_value) override;
	virtual Variant get_parameter(const StringName &p_name) const override;
	bool trigger(const StringName &p_name, float p_value = 1.0f);
	// Sample-accurate: fire at absolute graph-rate frame (0 = ASAP). N1 / P7 R.
	bool trigger_at(const StringName &p_name, uint64_t p_frame, float p_value = 1.0f);

	[[nodiscard]] float get_voice_cpu_microseconds() const;
	[[nodiscard]] float get_budget_percent() const;
	[[nodiscard]] float get_last_rms() const;
	// P8a: importance key shared with SymphonyVoicePool; tier protects VITAL from budget steal.
	void set_voice_importance(float p_importance);
	[[nodiscard]] float get_voice_importance() const;
	void set_voice_tier(int p_tier);
	[[nodiscard]] int get_voice_tier() const;
	[[nodiscard]] float get_estimated_cost_units() const;

	// Test/debug: push WavePlayer finished on the live package and mix once.
	bool fire_source_finished_and_mix(AudioFrame *p_buffer, int p_frames);

	[[nodiscard]] bool is_registered_with_voice_manager() const { return registered_with_manager; }
	[[nodiscard]] bool has_unsupported_seek() const { return unsupported_seek.load(std::memory_order_acquire); }

	// Per-voice stats for the manager (relaxed atomics; never touches package memory).
	[[nodiscard]] float get_cost_pct_ewma() const { return stat_cost_pct_ewma.load(std::memory_order_relaxed); }
	[[nodiscard]] uint64_t get_last_mix_seq() const { return last_mix_seq.load(std::memory_order_relaxed); }
	// Manager-only LOD floor change timestamp (UINT64_MAX = never changed).
	[[nodiscard]] uint64_t get_floor_changed_us() const { return floor_changed_us.load(std::memory_order_relaxed); }
	void set_budget_lod_floor_at(int32_t p_lod_tier, uint64_t p_now_us);

	// Test hook: seed the smoothed cost and mark the voice as freshly mixed.
	void _test_set_cost_ewma(float p_pct);

	~AudioStreamPlaybackSymphony();
};
