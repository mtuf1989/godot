#include "audio_stream_symphony.h"
#include "audio_stream_playback_symphony.h"
#include "../core/symphony_graph_compiler.h"

#include "core/object/class_db.h"

void AudioStreamSymphony::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mix_rate", "rate"), &AudioStreamSymphony::set_mix_rate);
	ClassDB::bind_method(D_METHOD("get_mix_rate"), &AudioStreamSymphony::get_mix_rate);
	ClassDB::bind_method(D_METHOD("load_test_graph"), &AudioStreamSymphony::load_test_graph);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mix_rate", PROPERTY_HINT_RANGE, "22050,96000,1"), "set_mix_rate", "get_mix_rate");
}

void AudioStreamSymphony::set_mix_rate(float p_mix_rate) {
	mix_rate = p_mix_rate;
}

float AudioStreamSymphony::get_mix_rate() const {
	return mix_rate;
}

void AudioStreamSymphony::set_graph_description(const GraphDescription &p_desc) {
	graph_desc = p_desc;
}

const GraphDescription &AudioStreamSymphony::get_graph_description() const {
	return graph_desc;
}

CompiledGraph *AudioStreamSymphony::compile_graph() const {
	GraphCompiler::CompileResult result = GraphCompiler::compile(graph_desc, mix_rate);
	if (!result.success()) {
		for (const String &err : result.errors) {
			ERR_PRINT(vformat("Symphony compile error: %s", err));
		}
		return nullptr;
	}
	return result.graph;
}

GraphDescription AudioStreamSymphony::build_test_graph_10_nodes() {
	// 10-node test graph:
	// [0] Constant(440) -> [2] Oscillator -> [4] Gain(0.3) -+
	// [1] Constant(880) -> [3] Oscillator -> [5] Gain(0.2) -+-> [6] MathAdd -> [7] Gain(0.5) -> [8] ADSR -> [9] GraphOutput
	//
	// ADSR trigger input is unconnected (optional) — envelope starts in ATTACK immediately
	// via a workaround: we set attack to 0 so it jumps to sustain level instantly.

	GraphDescription desc;

	// Node 0: Constant 440Hz
	NodeDesc n0;
	n0.id = 0;
	n0.type_name = "Constant";
	n0.params["value"] = 440.0f;
	desc.nodes.push_back(n0);

	// Node 1: Constant 880Hz
	NodeDesc n1;
	n1.id = 1;
	n1.type_name = "Constant";
	n1.params["value"] = 880.0f;
	desc.nodes.push_back(n1);

	// Node 2: Oscillator (freq from Constant 440)
	NodeDesc n2;
	n2.id = 2;
	n2.type_name = "Oscillator";
	desc.nodes.push_back(n2);

	// Node 3: Oscillator (freq from Constant 880)
	NodeDesc n3;
	n3.id = 3;
	n3.type_name = "Oscillator";
	desc.nodes.push_back(n3);

	// Node 4: Gain 0.3
	NodeDesc n4;
	n4.id = 4;
	n4.type_name = "Gain";
	n4.params["gain"] = 0.3f;
	desc.nodes.push_back(n4);

	// Node 5: Gain 0.2
	NodeDesc n5;
	n5.id = 5;
	n5.type_name = "Gain";
	n5.params["gain"] = 0.2f;
	desc.nodes.push_back(n5);

	// Node 6: MathAdd
	NodeDesc n6;
	n6.id = 6;
	n6.type_name = "MathAdd";
	desc.nodes.push_back(n6);

	// Node 7: Gain 0.5 (master volume)
	NodeDesc n7;
	n7.id = 7;
	n7.type_name = "Gain";
	n7.params["gain"] = 0.5f;
	desc.nodes.push_back(n7);

	// Node 8: ADSR (attack=0 so it starts at sustain immediately)
	NodeDesc n8;
	n8.id = 8;
	n8.type_name = "ADSR";
	n8.params["attack"] = 0.001f;
	n8.params["decay"] = 0.01f;
	n8.params["sustain"] = 1.0f;
	n8.params["release"] = 0.05f;
	desc.nodes.push_back(n8);

	// Node 9: GraphOutput
	NodeDesc n9;
	n9.id = 9;
	n9.type_name = "GraphOutput";
	desc.nodes.push_back(n9);

	// Connections:
	// Constant(440) -> Oscillator[0] frequency
	desc.connections.push_back({ 0, 0, 2, 0 });
	// Constant(880) -> Oscillator[1] frequency
	desc.connections.push_back({ 1, 0, 3, 0 });
	// Oscillator[0] -> Gain(0.3)
	desc.connections.push_back({ 2, 0, 4, 0 });
	// Oscillator[1] -> Gain(0.2)
	desc.connections.push_back({ 3, 0, 5, 0 });
	// Gain(0.3) -> MathAdd input a
	desc.connections.push_back({ 4, 0, 6, 0 });
	// Gain(0.2) -> MathAdd input b
	desc.connections.push_back({ 5, 0, 6, 1 });
	// MathAdd -> Gain(0.5)
	desc.connections.push_back({ 6, 0, 7, 0 });
	// Gain(0.5) -> ADSR input
	desc.connections.push_back({ 7, 0, 8, 0 });
	// ADSR -> GraphOutput
	desc.connections.push_back({ 8, 0, 9, 0 });

	return desc;
}

void AudioStreamSymphony::load_test_graph() {
	graph_desc = build_test_graph_10_nodes();
}

Ref<AudioStreamPlayback> AudioStreamSymphony::instantiate_playback() {
	Ref<AudioStreamPlaybackSymphony> playback;
	playback.instantiate();
	playback->stream = Ref<AudioStreamSymphony>(this);

	// If we have a graph description, compile and load it
	if (graph_desc.nodes.size() > 0) {
		CompiledGraph *compiled = compile_graph();
		if (compiled) {
			playback->swap_graph(compiled);
		}
	}

	return playback;
}

String AudioStreamSymphony::get_stream_name() const {
	return "Symphony";
}

double AudioStreamSymphony::get_length() const {
	return 0.0;
}

bool AudioStreamSymphony::is_monophonic() const {
	return false;
}
