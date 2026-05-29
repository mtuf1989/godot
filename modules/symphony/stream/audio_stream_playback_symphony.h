#pragma once

#include "servers/audio/audio_stream.h"
#include "audio_stream_symphony.h"
#include "../core/symphony_pin_types.h"
#include "../core/symphony_compiled_graph.h"
#include "../nodes/io/symphony_graph_output.h"

#include <atomic>

class AudioStreamPlaybackSymphony : public AudioStreamPlayback {
	GDCLASS(AudioStreamPlaybackSymphony, AudioStreamPlayback)
	friend class AudioStreamSymphony;

private:
	Ref<AudioStreamSymphony> stream;
	bool active = false;

	// The currently executing graph (owned, freed on main thread).
	CompiledGraph *current_graph = nullptr;

	// Atomic slot for hot-swap: main thread writes here, audio thread picks up.
	std::atomic<CompiledGraph *> pending_graph{ nullptr };

	// Graveyard: old graphs waiting to be freed on the main thread.
	// Simple approach: store one pointer, freed next time we swap or stop.
	CompiledGraph *graveyard = nullptr;

	// Pointer to the GraphOutput operator in the current graph (for set_output calls).
	SymphonyGraphOutput *graph_output_node = nullptr;

	void cleanup_graveyard();
	void find_graph_output();

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
	// Takes ownership of p_graph. The old graph will be freed on the main thread.
	void swap_graph(CompiledGraph *p_graph);

	// GDScript API stubs (Phase 3: signatures only, minimal implementation)
	void set_parameter(const StringName &p_name, float p_value);
	void trigger(const StringName &p_name, float p_value = 1.0f);

	~AudioStreamPlaybackSymphony();
};
