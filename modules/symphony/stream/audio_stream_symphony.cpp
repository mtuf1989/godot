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
	ClassDB::bind_method(D_METHOD("load_test_graph"), &AudioStreamSymphony::load_test_graph);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mix_rate", PROPERTY_HINT_RANGE, "22050,96000,1"), "set_mix_rate", "get_mix_rate");
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
