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
	int voice_priority = 50; // 0-100, higher = harder to steal
	GraphDescription graph_desc;

protected:
	static void _bind_methods();

	// Resource serialization for .tres/.res
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _get(const StringName &p_name, Variant &r_ret) const;
	bool _set(const StringName &p_name, const Variant &p_value);

public:
	void set_mix_rate(float p_mix_rate);
	float get_mix_rate() const;

	void set_voice_priority(int p_priority);
	int get_voice_priority() const;

	void set_graph_description(const GraphDescription &p_desc);
	const GraphDescription &get_graph_description() const;

	CompiledGraph *compile_graph() const;

	static GraphDescription build_test_graph_10_nodes();
	static GraphDescription build_test_graph_30_nodes();
	static GraphDescription build_test_graph_50_nodes();
	void load_test_graph();
	void load_test_graph_30();
	void load_test_graph_50();

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual double get_length() const override;
	virtual bool is_monophonic() const override;
};
