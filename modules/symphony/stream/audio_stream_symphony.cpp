#include "audio_stream_symphony.h"
#include "audio_stream_playback_symphony.h"
#include "../core/symphony_graph_compiler.h"
#include "../core/symphony_graph_flattener.h"
#include "../core/symphony_operator_registry.h"

#include "core/object/class_db.h"
#include "core/io/resource.h"

void AudioStreamSymphony::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mix_rate", "rate"), &AudioStreamSymphony::set_mix_rate);
	ClassDB::bind_method(D_METHOD("get_mix_rate"), &AudioStreamSymphony::get_mix_rate);
	ClassDB::bind_method(D_METHOD("set_voice_priority", "priority"), &AudioStreamSymphony::set_voice_priority);
	ClassDB::bind_method(D_METHOD("get_voice_priority"), &AudioStreamSymphony::get_voice_priority);
	ClassDB::bind_method(D_METHOD("load_test_graph"), &AudioStreamSymphony::load_test_graph);
	ClassDB::bind_method(D_METHOD("load_test_graph_30"), &AudioStreamSymphony::load_test_graph_30);
	ClassDB::bind_method(D_METHOD("load_test_graph_50"), &AudioStreamSymphony::load_test_graph_50);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mix_rate", PROPERTY_HINT_RANGE, "22050,96000,1"), "set_mix_rate", "get_mix_rate");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "voice_priority", PROPERTY_HINT_RANGE, "0,100,1"), "set_voice_priority", "get_voice_priority");
}

// --- Resource serialization ---

void AudioStreamSymphony::_get_property_list(List<PropertyInfo> *p_list) const {
	// node_count — so the loader knows how many nodes to expect
	p_list->push_back(PropertyInfo(Variant::INT, "graph/node_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));

	for (int32_t i = 0; i < graph_desc.nodes.size(); i++) {
		String prefix = vformat("graph/nodes/%d/", i);
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::STRING_NAME, prefix + "type", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::VECTOR2, prefix + "position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::DICTIONARY, prefix + "params", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::BOOL, prefix + "collapsed", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
	}

	// connections
	p_list->push_back(PropertyInfo(Variant::INT, "graph/connection_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));

	for (int32_t i = 0; i < graph_desc.connections.size(); i++) {
		String prefix = vformat("graph/connections/%d/", i);
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "from_node", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "from_pin", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "to_node", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "to_pin", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
	}

	// frames
	p_list->push_back(PropertyInfo(Variant::INT, "graph/frame_count", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));

	for (int32_t i = 0; i < graph_desc.frames.size(); i++) {
		String prefix = vformat("graph/frames/%d/", i);
		p_list->push_back(PropertyInfo(Variant::INT, prefix + "id", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::STRING, prefix + "title", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::VECTOR2, prefix + "position", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::VECTOR2, prefix + "size", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::COLOR, prefix + "tint_color", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		p_list->push_back(PropertyInfo(Variant::PACKED_INT32_ARRAY, prefix + "attached_nodes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
	}
}

bool AudioStreamSymphony::_get(const StringName &p_name, Variant &r_ret) const {
	String name = String(p_name);

	if (name == "graph/node_count") {
		r_ret = graph_desc.nodes.size();
		return true;
	}
	if (name == "graph/connection_count") {
		r_ret = graph_desc.connections.size();
		return true;
	}
	if (name == "graph/frame_count") {
		r_ret = graph_desc.frames.size();
		return true;
	}

	if (name.begins_with("graph/nodes/")) {
		// Parse: graph/nodes/<idx>/<field>
		String rest = name.substr(String("graph/nodes/").length());
		int slash = rest.find("/");
		if (slash < 0) {
			return false;
		}
		int idx = rest.substr(0, slash).to_int();
		String field = rest.substr(slash + 1);

		if (idx < 0 || idx >= graph_desc.nodes.size()) {
			return false;
		}
		const NodeDesc &nd = graph_desc.nodes[idx];

		if (field == "id") {
			r_ret = nd.id;
			return true;
		} else if (field == "type") {
			r_ret = nd.type_name;
			return true;
		} else if (field == "position") {
			r_ret = nd.editor_position;
			return true;
		} else if (field == "params") {
			Dictionary d;
			for (const KeyValue<StringName, Variant> &kv : nd.params) {
				d[String(kv.key)] = kv.value;
			}
			r_ret = d;
			return true;
		} else if (field == "collapsed") {
			r_ret = nd.collapsed;
			return true;
		}
	}

	if (name.begins_with("graph/connections/")) {
		String rest = name.substr(String("graph/connections/").length());
		int slash = rest.find("/");
		if (slash < 0) {
			return false;
		}
		int idx = rest.substr(0, slash).to_int();
		String field = rest.substr(slash + 1);

		if (idx < 0 || idx >= graph_desc.connections.size()) {
			return false;
		}
		const ConnectionDesc &conn = graph_desc.connections[idx];

		if (field == "from_node") {
			r_ret = conn.from_node;
			return true;
		} else if (field == "from_pin") {
			r_ret = conn.from_pin;
			return true;
		} else if (field == "to_node") {
			r_ret = conn.to_node;
			return true;
		} else if (field == "to_pin") {
			r_ret = conn.to_pin;
			return true;
		}
	}

	if (name.begins_with("graph/frames/")) {
		String rest = name.substr(String("graph/frames/").length());
		int slash = rest.find("/");
		if (slash < 0) {
			return false;
		}
		int idx = rest.substr(0, slash).to_int();
		String field = rest.substr(slash + 1);

		if (idx < 0 || idx >= graph_desc.frames.size()) {
			return false;
		}
		const FrameDesc &fd = graph_desc.frames[idx];

		if (field == "id") {
			r_ret = fd.id;
			return true;
		} else if (field == "title") {
			r_ret = fd.title;
			return true;
		} else if (field == "position") {
			r_ret = fd.editor_position;
			return true;
		} else if (field == "size") {
			r_ret = fd.editor_size;
			return true;
		} else if (field == "tint_color") {
			r_ret = fd.tint_color;
			return true;
		} else if (field == "attached_nodes") {
			PackedInt32Array arr;
			for (int32_t nid : fd.attached_nodes) {
				arr.push_back(nid);
			}
			r_ret = arr;
			return true;
		}
	}

	return false;
}

bool AudioStreamSymphony::_set(const StringName &p_name, const Variant &p_value) {
	String name = String(p_name);

	if (name == "graph/node_count") {
		int count = p_value;
		graph_desc.nodes.resize(count);
		return true;
	}
	if (name == "graph/connection_count") {
		int count = p_value;
		graph_desc.connections.resize(count);
		return true;
	}
	if (name == "graph/frame_count") {
		int count = p_value;
		graph_desc.frames.resize(count);
		return true;
	}

	if (name.begins_with("graph/nodes/")) {
		String rest = name.substr(String("graph/nodes/").length());
		int slash = rest.find("/");
		if (slash < 0) {
			return false;
		}
		int idx = rest.substr(0, slash).to_int();
		String field = rest.substr(slash + 1);

		if (idx < 0 || idx >= graph_desc.nodes.size()) {
			return false;
		}
		NodeDesc &nd = graph_desc.nodes.write[idx];

		if (field == "id") {
			nd.id = p_value;
			return true;
		} else if (field == "type") {
			nd.type_name = p_value;
			return true;
		} else if (field == "position") {
			nd.editor_position = p_value;
			return true;
		} else if (field == "params") {
			Dictionary d = p_value;
			nd.params.clear();
			LocalVector<Variant> keys = d.get_key_list();
			for (const Variant &key : keys) {
				nd.params[StringName(String(key))] = d[key];
			}
			return true;
		} else if (field == "collapsed") {
			nd.collapsed = p_value;
			return true;
		}
	}

	if (name.begins_with("graph/connections/")) {
		String rest = name.substr(String("graph/connections/").length());
		int slash = rest.find("/");
		if (slash < 0) {
			return false;
		}
		int idx = rest.substr(0, slash).to_int();
		String field = rest.substr(slash + 1);

		if (idx < 0 || idx >= graph_desc.connections.size()) {
			return false;
		}
		ConnectionDesc &conn = graph_desc.connections.write[idx];

		if (field == "from_node") {
			conn.from_node = p_value;
			return true;
		} else if (field == "from_pin") {
			conn.from_pin = p_value;
			return true;
		} else if (field == "to_node") {
			conn.to_node = p_value;
			return true;
		} else if (field == "to_pin") {
			conn.to_pin = p_value;
			return true;
		}
	}

	if (name.begins_with("graph/frames/")) {
		String rest = name.substr(String("graph/frames/").length());
		int slash = rest.find("/");
		if (slash < 0) {
			return false;
		}
		int idx = rest.substr(0, slash).to_int();
		String field = rest.substr(slash + 1);

		if (idx < 0 || idx >= graph_desc.frames.size()) {
			return false;
		}
		FrameDesc &fd = graph_desc.frames.write[idx];

		if (field == "id") {
			fd.id = p_value;
			return true;
		} else if (field == "title") {
			fd.title = p_value;
			return true;
		} else if (field == "position") {
			fd.editor_position = p_value;
			return true;
		} else if (field == "size") {
			fd.editor_size = p_value;
			return true;
		} else if (field == "tint_color") {
			fd.tint_color = p_value;
			return true;
		} else if (field == "attached_nodes") {
			PackedInt32Array arr = p_value;
			fd.attached_nodes.clear();
			for (int i = 0; i < arr.size(); i++) {
				fd.attached_nodes.push_back(arr[i]);
			}
			return true;
		}
	}

	return false;
}

void AudioStreamSymphony::set_mix_rate(float p_mix_rate) {
	mix_rate = p_mix_rate;
}

float AudioStreamSymphony::get_mix_rate() const {
	return mix_rate;
}

void AudioStreamSymphony::set_voice_priority(int p_priority) {
	voice_priority = CLAMP(p_priority, 0, 100);
}

int AudioStreamSymphony::get_voice_priority() const {
	return voice_priority;
}

void AudioStreamSymphony::set_graph_description(const GraphDescription &p_desc) {
	graph_desc = p_desc;
}

const GraphDescription &AudioStreamSymphony::get_graph_description() const {
	return graph_desc;
}

CompiledGraph *AudioStreamSymphony::compile_graph() const {
	// Pre-pass: flatten any SubGraph nodes into a single graph.
	String owner_path = get_path();
	GraphFlattener::FlattenResult flat = GraphFlattener::flatten(graph_desc, owner_path);
	if (!flat.success()) {
		for (const String &err : flat.errors) {
			ERR_PRINT(vformat("Symphony flatten error: %s", err));
		}
		return nullptr;
	}

	GraphCompiler::CompileResult result = GraphCompiler::compile(flat.graph, mix_rate);
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

void AudioStreamSymphony::load_test_graph_30() {
	graph_desc = build_test_graph_30_nodes();
}

void AudioStreamSymphony::load_test_graph_50() {
	graph_desc = build_test_graph_50_nodes();
}

GraphDescription AudioStreamSymphony::build_test_graph_30_nodes() {
	// 30-node graph: 3 oscillator voices, each with filter + envelope, mixed together,
	// then through a compressor chain.
	// Voice A: Const(220) -> Osc -> BiquadLP -> Gain -> ADSR
	// Voice B: Const(330) -> Osc -> BiquadHP -> Gain -> ADSR
	// Voice C: Const(550) -> Osc -> BiquadLP -> Gain -> ADSR
	// LFO -> OnePole (smoothed) -> Voice A filter cutoff
	// Mix A+B -> Mix (A+B)+C -> Gain -> Compressor -> DCBlocker -> Saturator -> Gain -> GraphOutput
	// Plus extra constants for filter cutoffs and gain staging = 30+ nodes

	GraphDescription desc;
	int32_t id = 0;

	auto add_node = [&](const char *type, const HashMap<StringName, Variant> &params = {}) -> int32_t {
		NodeDesc n;
		n.id = id;
		n.type_name = type;
		n.params = params;
		desc.nodes.push_back(n);
		return id++;
	};

	auto connect = [&](int32_t from, int32_t from_pin, int32_t to, int32_t to_pin) {
		desc.connections.push_back({ from, from_pin, to, to_pin });
	};

	auto make_params = [](std::initializer_list<std::pair<const char *, float>> list) {
		HashMap<StringName, Variant> p;
		for (auto &kv : list) {
			p[StringName(kv.first)] = kv.second;
		}
		return p;
	};

	// Voice A: nodes 0-4
	int32_t const_a = add_node("Constant", make_params({ { "value", 220.0f } }));
	int32_t osc_a = add_node("Oscillator");
	int32_t filt_a = add_node("BiquadFilter", make_params({ { "mode", 0.0f }, { "cutoff", 2000.0f }, { "resonance", 1.5f } }));
	int32_t gain_a = add_node("Gain", make_params({ { "gain", 0.4f } }));
	int32_t adsr_a = add_node("ADSR", make_params({ { "attack", 0.001f }, { "decay", 0.01f }, { "sustain", 1.0f }, { "release", 0.05f } }));

	connect(const_a, 0, osc_a, 0);
	connect(osc_a, 0, filt_a, 0);
	connect(filt_a, 0, gain_a, 0);
	connect(gain_a, 0, adsr_a, 0);

	// Voice B: nodes 5-9
	int32_t const_b = add_node("Constant", make_params({ { "value", 330.0f } }));
	int32_t osc_b = add_node("Oscillator");
	int32_t filt_b = add_node("BiquadFilter", make_params({ { "mode", 1.0f }, { "cutoff", 800.0f }, { "resonance", 0.707f } }));
	int32_t gain_b = add_node("Gain", make_params({ { "gain", 0.3f } }));
	int32_t adsr_b = add_node("ADSR", make_params({ { "attack", 0.001f }, { "decay", 0.01f }, { "sustain", 1.0f }, { "release", 0.05f } }));

	connect(const_b, 0, osc_b, 0);
	connect(osc_b, 0, filt_b, 0);
	connect(filt_b, 0, gain_b, 0);
	connect(gain_b, 0, adsr_b, 0);

	// Voice C: nodes 10-14
	int32_t const_c = add_node("Constant", make_params({ { "value", 550.0f } }));
	int32_t osc_c = add_node("Oscillator");
	int32_t filt_c = add_node("BiquadFilter", make_params({ { "mode", 0.0f }, { "cutoff", 3000.0f }, { "resonance", 2.0f } }));
	int32_t gain_c = add_node("Gain", make_params({ { "gain", 0.3f } }));
	int32_t adsr_c = add_node("ADSR", make_params({ { "attack", 0.001f }, { "decay", 0.01f }, { "sustain", 1.0f }, { "release", 0.05f } }));

	connect(const_c, 0, osc_c, 0);
	connect(osc_c, 0, filt_c, 0);
	connect(filt_c, 0, gain_c, 0);
	connect(gain_c, 0, adsr_c, 0);

	// LFO -> OnePole -> Voice A filter cutoff modulation: nodes 15-17
	int32_t lfo_freq = add_node("Constant", make_params({ { "value", 2000.0f } })); // Cutoff center
	int32_t lfo = add_node("LFO", make_params({ { "rate", 3.0f }, { "waveform", 0.0f } }));
	int32_t lfo_smooth = add_node("OnePole", make_params({ { "cutoff", 10.0f } }));

	connect(lfo, 0, lfo_smooth, 0); // LFO FLOAT -> OnePole (Float->Audio promotion)
	connect(lfo_smooth, 0, filt_a, 1); // Smoothed LFO -> filter A cutoff
	connect(lfo_freq, 0, filt_b, 1); // Constant cutoff -> filter B cutoff

	// Mix chain: nodes 18-19
	int32_t mix_ab = add_node("MathAdd");
	int32_t mix_abc = add_node("MathAdd");

	connect(adsr_a, 0, mix_ab, 0);
	connect(adsr_b, 0, mix_ab, 1);
	connect(mix_ab, 0, mix_abc, 0);
	connect(adsr_c, 0, mix_abc, 1);

	// Master chain: nodes 20-26
	int32_t master_gain = add_node("Gain", make_params({ { "gain", 0.5f } }));
	int32_t comp = add_node("Compressor", make_params({ { "threshold", -12.0f }, { "ratio", 4.0f }, { "attack", 0.01f }, { "release", 0.1f } }));
	int32_t dc_block = add_node("DCBlocker");
	int32_t sat = add_node("Saturator", make_params({ { "drive", 1.5f } }));
	int32_t post_gain = add_node("Gain", make_params({ { "gain", 0.3f } }));
	int32_t post_filt = add_node("BiquadFilter", make_params({ { "mode", 0.0f }, { "cutoff", 16000.0f }, { "resonance", 0.707f } }));
	int32_t output = add_node("GraphOutput");

	connect(mix_abc, 0, master_gain, 0);
	connect(master_gain, 0, comp, 0);
	connect(comp, 0, dc_block, 0);
	connect(dc_block, 0, sat, 0);
	connect(sat, 0, post_gain, 0);
	connect(post_gain, 0, post_filt, 0);

	// Extra padding nodes to reach 30: noise + filter + gain (silent but processed)
	int32_t noise = add_node("Noise");
	int32_t noise_filt = add_node("BiquadFilter", make_params({ { "mode", 0.0f }, { "cutoff", 500.0f }, { "resonance", 1.0f } }));
	int32_t noise_gain = add_node("Gain", make_params({ { "gain", 0.0f } })); // Silent

	connect(noise, 0, noise_filt, 0);
	connect(noise_filt, 0, noise_gain, 0);

	// Mix post_filt + silent noise -> output
	int32_t final_mix = add_node("MathAdd");
	connect(post_filt, 0, final_mix, 0);
	connect(noise_gain, 0, final_mix, 1);
	connect(final_mix, 0, output, 0);

	return desc;
}

GraphDescription AudioStreamSymphony::build_test_graph_50_nodes() {
	// 50-node graph: 5 oscillator voices with individual filter chains,
	// mixed in a tree, through a multi-stage master chain.

	GraphDescription desc;
	int32_t id = 0;

	auto add_node = [&](const char *type, const HashMap<StringName, Variant> &params = {}) -> int32_t {
		NodeDesc n;
		n.id = id;
		n.type_name = type;
		n.params = params;
		desc.nodes.push_back(n);
		return id++;
	};

	auto connect = [&](int32_t from, int32_t from_pin, int32_t to, int32_t to_pin) {
		desc.connections.push_back({ from, from_pin, to, to_pin });
	};

	auto make_params = [](std::initializer_list<std::pair<const char *, float>> list) {
		HashMap<StringName, Variant> p;
		for (auto &kv : list) {
			p[StringName(kv.first)] = kv.second;
		}
		return p;
	};

	// 5 voices, each: Constant -> Oscillator -> BiquadFilter -> Gain -> ADSR -> OnePole (8 nodes each = 40)
	float freqs[5] = { 110.0f, 220.0f, 330.0f, 440.0f, 660.0f };
	float cutoffs[5] = { 1000.0f, 2000.0f, 1500.0f, 3000.0f, 4000.0f };
	int32_t voice_outputs[5];

	for (int v = 0; v < 5; v++) {
		int32_t freq_const = add_node("Constant", make_params({ { "value", freqs[v] } }));
		int32_t osc = add_node("Oscillator");
		int32_t filt = add_node("BiquadFilter", make_params({ { "mode", 0.0f }, { "cutoff", cutoffs[v] }, { "resonance", 1.2f } }));
		int32_t gain = add_node("Gain", make_params({ { "gain", 0.2f } }));
		int32_t adsr = add_node("ADSR", make_params({ { "attack", 0.001f }, { "decay", 0.02f }, { "sustain", 1.0f }, { "release", 0.05f } }));
		int32_t smooth = add_node("OnePole", make_params({ { "cutoff", 8000.0f } }));
		// Extra: second filter stage for more processing
		int32_t filt2 = add_node("BiquadFilter", make_params({ { "mode", 0.0f }, { "cutoff", cutoffs[v] * 1.5f }, { "resonance", 0.707f } }));
		int32_t gain2 = add_node("Gain", make_params({ { "gain", 0.8f } }));

		connect(freq_const, 0, osc, 0);
		connect(osc, 0, filt, 0);
		connect(filt, 0, gain, 0);
		connect(gain, 0, adsr, 0);
		connect(adsr, 0, smooth, 0);
		connect(smooth, 0, filt2, 0);
		connect(filt2, 0, gain2, 0);

		voice_outputs[v] = gain2;
	}

	// Mix tree: 4 MathAdd nodes to combine 5 voices (nodes 40-43)
	int32_t mix01 = add_node("MathAdd");
	int32_t mix23 = add_node("MathAdd");
	int32_t mix0123 = add_node("MathAdd");
	int32_t mix_all = add_node("MathAdd");

	connect(voice_outputs[0], 0, mix01, 0);
	connect(voice_outputs[1], 0, mix01, 1);
	connect(voice_outputs[2], 0, mix23, 0);
	connect(voice_outputs[3], 0, mix23, 1);
	connect(mix01, 0, mix0123, 0);
	connect(mix23, 0, mix0123, 1);
	connect(mix0123, 0, mix_all, 0);
	connect(voice_outputs[4], 0, mix_all, 1);

	// Master chain: Gain -> Compressor -> DCBlocker -> Saturator -> BiquadLP -> Gain -> GraphOutput (nodes 44-50)
	int32_t master_gain = add_node("Gain", make_params({ { "gain", 0.4f } }));
	int32_t comp = add_node("Compressor", make_params({ { "threshold", -10.0f }, { "ratio", 3.0f }, { "attack", 0.005f }, { "release", 0.08f } }));
	int32_t dc = add_node("DCBlocker");
	int32_t sat = add_node("Saturator", make_params({ { "drive", 1.2f } }));
	int32_t master_filt = add_node("BiquadFilter", make_params({ { "mode", 0.0f }, { "cutoff", 18000.0f }, { "resonance", 0.707f } }));
	int32_t final_gain = add_node("Gain", make_params({ { "gain", 0.25f } }));
	int32_t output = add_node("GraphOutput");

	connect(mix_all, 0, master_gain, 0);
	connect(master_gain, 0, comp, 0);
	connect(comp, 0, dc, 0);
	connect(dc, 0, sat, 0);
	connect(sat, 0, master_filt, 0);
	connect(master_filt, 0, final_gain, 0);
	connect(final_gain, 0, output, 0);

	return desc;
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



double AudioStreamSymphony::get_length() const {
	return 0.0;
}

bool AudioStreamSymphony::is_monophonic() const {
	return false;
}
