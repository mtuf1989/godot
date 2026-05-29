#pragma once

#include "servers/audio/audio_stream.h"
#include "../core/symphony_graph_description.h"
#include "../core/symphony_compiled_graph.h"

class AudioStreamPlaybackSymphony;

class AudioStreamSymphony : public AudioStream {
	GDCLASS(AudioStreamSymphony, AudioStream)

private:
	friend class AudioStreamPlaybackSymphony;
	float mix_rate = 44100.0f;
	GraphDescription graph_desc;

protected:
	static void _bind_methods();

public:
	void set_mix_rate(float p_mix_rate);
	float get_mix_rate() const;

	// Set the graph description (programmatic API for Phase 3).
	void set_graph_description(const GraphDescription &p_desc);
	const GraphDescription &get_graph_description() const;

	// Compile the current graph description into a CompiledGraph.
	// Returns nullptr on failure (errors printed to console).
	CompiledGraph *compile_graph() const;

	// Build a hardcoded 10-node test graph for Phase 3 validation.
	static GraphDescription build_test_graph_10_nodes();

	// GDScript-callable: load the 10-node test graph.
	void load_test_graph();

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual String get_stream_name() const override;
	virtual double get_length() const override;
	virtual bool is_monophonic() const override;
};
