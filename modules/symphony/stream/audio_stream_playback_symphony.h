#pragma once

#include "servers/audio/audio_stream.h"
#include "audio_stream_symphony.h"
#include "../core/symphony_pin_types.h"
#include "../core/symphony_compiled_graph.h"
#include "../nodes/io/symphony_graph_output.h"
#include "../nodes/io/symphony_graph_input.h"
#include "../nodes/io/symphony_trigger_input.h"

#include <atomic>

class AudioStreamPlaybackSymphony : public AudioStreamPlayback {
	GDCLASS(AudioStreamPlaybackSymphony, AudioStreamPlayback)
	friend class AudioStreamSymphony;
	friend class SymphonyVoiceManager;

private:
	Ref<AudioStreamSymphony> stream;
	std::atomic<bool> active{ false };
	bool registered_with_manager = false;
	std::atomic<bool> stop_pending{ false }; // Deferred stop: graph deletion happens in mix()

	// The currently executing graph (owned, freed on audio thread via deferred stop).
	CompiledGraph *current_graph = nullptr;

	// Atomic slot for hot-swap: main thread writes here, audio thread picks up.
	std::atomic<CompiledGraph *> pending_graph{ nullptr };

	// When true, the pending_graph is an LOD transition (crossfade needed).
	// When false, it's a regular hot-swap (graveyard pattern).
	std::atomic<bool> pending_is_lod{ false };

	// Graveyard: old graphs waiting to be freed on the main thread.
	CompiledGraph *graveyard = nullptr;

	// --- LOD crossfade (Option B: parallel execution during transition) ---
	CompiledGraph *lod_outgoing_graph = nullptr;   // Old graph being faded out
	SymphonyGraphOutput *lod_outgoing_output = nullptr;
	float lod_crossfade_progress = 1.0f;  // 0 = fully outgoing, 1 = fully current (no crossfade)
	float lod_crossfade_speed = 0.0f;     // Progress increment per sample
	int current_lod_tier = 0;             // Current active LOD (0=full, 1=simplified, 2=minimal)
	static constexpr int LOD_CROSSFADE_SAMPLES = 2048; // ~42ms at 48kHz

	// Pointer to the GraphOutput operator in the current graph.
	SymphonyGraphOutput *graph_output_node = nullptr;

	// Parameter/trigger routing tables (rebuilt on graph swap).
	HashMap<StringName, SymphonyGraphInput *> parameter_map;
	HashMap<StringName, SymphonyTriggerInput *> trigger_map;

	// --- Profiling ---
	float last_mix_time_us = 0.0f;   // Total mix() time in microseconds
	float last_rms = 0.0f;           // RMS of last mix() output
	int32_t last_frame_count = 0;    // Frame count of last mix() call
	float mix_rate_cached = 44100.0f;

	void cleanup_graveyard();
	void find_graph_output();
	void rebuild_routing_tables();
	void _finalize_stop(); // Deferred graph deletion — always called from audio thread context

protected:
	static void _bind_methods();

public:
	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual bool is_playing() const override;
	virtual int get_loop_count() const override;
	virtual double get_playback_position() const override;
	virtual void seek(double p_time) override;
	virtual int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;

	// Hot-swap: publish a new compiled graph (called from main thread).
	void swap_graph(CompiledGraph *p_graph);

	// LOD transition: initiate parallel crossfade to a new LOD tier (called from main thread).
	void transition_to_lod(int p_lod_tier);
	[[nodiscard]] int get_current_lod_tier() const { return current_lod_tier; }
	[[nodiscard]] bool is_lod_transitioning() const { return lod_crossfade_progress < 1.0f; }

	// GDScript API — overrides AudioStreamPlayback virtuals.
	virtual void set_parameter(const StringName &p_name, const Variant &p_value) override;
	void trigger(const StringName &p_name, float p_value = 1.0f);

	// Profiling API
	[[nodiscard]] float get_voice_cpu_microseconds() const;
	[[nodiscard]] float get_budget_percent() const;
	[[nodiscard]] float get_last_rms() const;
	[[nodiscard]] int get_effective_priority() const;

	~AudioStreamPlaybackSymphony();
};
