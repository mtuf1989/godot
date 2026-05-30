#include "symphony_graph_flattener.h"
#include "../stream/audio_stream_symphony.h"
#include "core/io/resource_loader.h"
#include "core/templates/hash_set.h"

struct FlattenContext {
	HashSet<String> visited_paths; // For cycle detection
	int32_t depth = 0;
	int32_t next_id = 0; // Global ID counter for remapped nodes
};

// Collect info about a sub-graph's interface (its GraphInput/GraphOutput nodes).
struct SubGraphPin {
	int32_t node_id = -1; // ID of the GraphInput/GraphOutput node inside the sub-graph
	int32_t sort_order = 0;
	float editor_y = 0.0f;
	String name;
};

struct SubGraphPinCompare {
	bool operator()(const SubGraphPin &a, const SubGraphPin &b) const {
		if (a.sort_order != b.sort_order) {
			return a.sort_order < b.sort_order;
		}
		if (a.editor_y != b.editor_y) {
			return a.editor_y < b.editor_y;
		}
		return a.name < b.name;
	}
};

static GraphFlattener::FlattenResult _flatten_recursive(const GraphDescription &p_desc, const String &p_owner_path, FlattenContext &ctx);

GraphFlattener::FlattenResult GraphFlattener::flatten(const GraphDescription &p_desc, const String &p_owner_path) {
	FlattenContext ctx;
	ctx.depth = 0;
	// Find max existing ID to start remapping above it.
	ctx.next_id = 0;
	for (const NodeDesc &nd : p_desc.nodes) {
		if (nd.id >= ctx.next_id) {
			ctx.next_id = nd.id + 1;
		}
	}
	if (!p_owner_path.is_empty()) {
		ctx.visited_paths.insert(p_owner_path);
	}
	return _flatten_recursive(p_desc, p_owner_path, ctx);
}

static GraphFlattener::FlattenResult _flatten_recursive(const GraphDescription &p_desc, const String &p_owner_path, FlattenContext &ctx) {
	GraphFlattener::FlattenResult result;

	if (ctx.depth > GraphFlattener::MAX_RECURSION_DEPTH) {
		result.errors.push_back(vformat("SubGraph recursion depth exceeded (%d). Possible circular reference.", GraphFlattener::MAX_RECURSION_DEPTH));
		return result;
	}

	// Separate SubGraph nodes from regular nodes.
	Vector<int32_t> subgraph_indices;
	for (int32_t i = 0; i < p_desc.nodes.size(); i++) {
		if (p_desc.nodes[i].type_name == StringName("SubGraph")) {
			subgraph_indices.push_back(i);
		} else {
			result.graph.nodes.push_back(p_desc.nodes[i]);
		}
	}

	// If no SubGraph nodes, just copy connections and return.
	if (subgraph_indices.is_empty()) {
		result.graph.connections = p_desc.connections;
		result.graph.frames = p_desc.frames;
		return result;
	}

	// Copy frames (they don't reference sub-graph internals).
	result.graph.frames = p_desc.frames;

	// Build a set of SubGraph node IDs for quick lookup.
	HashMap<int32_t, int32_t> subgraph_id_to_desc_idx; // node_id -> index in p_desc.nodes
	for (int32_t si : subgraph_indices) {
		subgraph_id_to_desc_idx.insert(p_desc.nodes[si].id, si);
	}

	// For each SubGraph node, load and flatten the referenced graph, then inline it.
	// We need to track: for each SubGraph node_id, the mapping of its pins to internal node IDs.
	struct InlinedSubGraph {
		// Maps pin index (sorted) -> internal node ID that handles that pin.
		Vector<int32_t> input_node_ids;  // GraphInput nodes (become input pins on SubGraph)
		Vector<int32_t> output_node_ids; // GraphOutput nodes (become output pins on SubGraph)
	};
	HashMap<int32_t, InlinedSubGraph> inlined_map; // SubGraph node_id -> inlined info

	for (int32_t si : subgraph_indices) {
		const NodeDesc &sg_node = p_desc.nodes[si];
		String resource_path;
		if (sg_node.params.has("resource_path")) {
			resource_path = String(sg_node.params["resource_path"]);
		}

		if (resource_path.is_empty()) {
			result.errors.push_back(vformat("SubGraph node %d: resource_path is empty.", sg_node.id));
			return result;
		}

		// Cycle detection.
		if (ctx.visited_paths.has(resource_path)) {
			String chain;
			for (const String &p : ctx.visited_paths) {
				if (!chain.is_empty()) {
					chain += " -> ";
				}
				chain += p;
			}
			chain += " -> " + resource_path;
			result.errors.push_back(vformat("Circular SubGraph reference detected: %s", chain));
			return result;
		}

		// Load the referenced resource.
		Ref<AudioStreamSymphony> sub_resource = ResourceLoader::load(resource_path, "AudioStreamSymphony");
		if (sub_resource.is_null()) {
			result.errors.push_back(vformat("SubGraph node %d: failed to load resource '%s'.", sg_node.id, resource_path));
			return result;
		}

		const GraphDescription &sub_desc = sub_resource->get_graph_description();
		if (sub_desc.nodes.is_empty()) {
			result.errors.push_back(vformat("SubGraph node %d: referenced graph '%s' has no nodes.", sg_node.id, resource_path));
			return result;
		}

		// Recursively flatten the sub-graph.
		ctx.visited_paths.insert(resource_path);
		ctx.depth++;

		GraphFlattener::FlattenResult sub_result = _flatten_recursive(sub_desc, resource_path, ctx);

		ctx.depth--;
		ctx.visited_paths.erase(resource_path);

		if (!sub_result.success()) {
			for (const String &err : sub_result.errors) {
				result.errors.push_back(vformat("In SubGraph '%s' (node %d): %s", resource_path, sg_node.id, err));
			}
			return result;
		}

		// Remap IDs in the flattened sub-graph and collect interface nodes.
		HashMap<int32_t, int32_t> id_remap; // old_id -> new_id
		InlinedSubGraph inlined;

		// Collect GraphInput/GraphOutput nodes with their sort info.
		Vector<SubGraphPin> input_pins;
		Vector<SubGraphPin> output_pins;

		for (NodeDesc &nd : sub_result.graph.nodes) {
			int32_t new_id = ctx.next_id++;
			id_remap.insert(nd.id, new_id);
			nd.id = new_id;

			if (nd.type_name == StringName("GraphInput")) {
				SubGraphPin pin;
				pin.node_id = new_id;
				pin.sort_order = nd.params.has("sort_order") ? (int32_t)(float)nd.params["sort_order"] : 0;
				pin.editor_y = nd.editor_position.y;
				pin.name = nd.params.has("parameter_name") ? String(nd.params["parameter_name"]) : "";
				input_pins.push_back(pin);
			} else if (nd.type_name == StringName("GraphOutput")) {
				SubGraphPin pin;
				pin.node_id = new_id;
				pin.sort_order = nd.params.has("sort_order") ? (int32_t)(float)nd.params["sort_order"] : 0;
				pin.editor_y = nd.editor_position.y;
				pin.name = nd.params.has("display_name") ? String(nd.params["display_name"]) : "";
				output_pins.push_back(pin);
			}
		}

		// Sort pins by (sort_order, editor_y, name).
		input_pins.sort_custom<SubGraphPinCompare>();
		output_pins.sort_custom<SubGraphPinCompare>();

		for (const SubGraphPin &p : input_pins) {
			inlined.input_node_ids.push_back(p.node_id);
		}
		for (const SubGraphPin &p : output_pins) {
			inlined.output_node_ids.push_back(p.node_id);
		}

		// Remap connections within the sub-graph.
		for (ConnectionDesc &conn : sub_result.graph.connections) {
			conn.from_node = id_remap[conn.from_node];
			conn.to_node = id_remap[conn.to_node];
		}

		// Add the sub-graph's nodes and connections to our result.
		for (const NodeDesc &nd : sub_result.graph.nodes) {
			result.graph.nodes.push_back(nd);
		}
		for (const ConnectionDesc &conn : sub_result.graph.connections) {
			result.graph.connections.push_back(conn);
		}

		inlined_map.insert(sg_node.id, inlined);
	}

	// Now rewire connections from the parent that referenced SubGraph nodes.
	// A connection TO a SubGraph node's input pin N → connects to the Nth GraphInput node's input.
	//   But GraphInput has no inputs — it's a source. So we need to wire the source
	//   directly to whatever the GraphInput's output was connected to.
	//   Actually: the parent connects FROM something → TO SubGraph pin N.
	//   This means the parent wants to feed data INTO the sub-graph's Nth input.
	//   The GraphInput node inside the sub-graph PRODUCES that data.
	//   So we replace: parent_source → SubGraph:pin_N
	//   With: parent_source → (whatever the GraphInput node's output was connected to inside the sub-graph)
	//   But wait — the GraphInput node already has its output wired to internal nodes.
	//   We need to REPLACE the GraphInput node with a pass-through from the parent source.
	//
	// Strategy: For connections TO a SubGraph node at pin N:
	//   - Find the Nth GraphInput node (by sorted order)
	//   - Find all connections FROM that GraphInput node inside the sub-graph
	//   - Replace them with connections FROM the original source node
	//   - Remove the GraphInput node from the result
	//
	// For connections FROM a SubGraph node at pin N:
	//   - Find the Nth GraphOutput node (by sorted order)
	//   - Find the connection TO that GraphOutput node inside the sub-graph
	//   - Replace the destination with the original destination node
	//   - Remove the GraphOutput node from the result

	// First, collect all parent connections involving SubGraph nodes.
	Vector<ConnectionDesc> parent_to_subgraph;   // connections going INTO a SubGraph
	Vector<ConnectionDesc> parent_from_subgraph; // connections coming FROM a SubGraph

	for (const ConnectionDesc &conn : p_desc.connections) {
		if (subgraph_id_to_desc_idx.has(conn.to_node)) {
			parent_to_subgraph.push_back(conn);
		} else if (subgraph_id_to_desc_idx.has(conn.from_node)) {
			parent_from_subgraph.push_back(conn);
		} else {
			// Normal connection between non-SubGraph nodes — keep as-is.
			result.graph.connections.push_back(conn);
		}
	}

	// Handle connections INTO SubGraph nodes.
	// For each: source_node:source_pin → SubGraph:pin_N
	// Replace GraphInput[N]'s output connections with source_node:source_pin as the source.
	for (const ConnectionDesc &conn : parent_to_subgraph) {
		const InlinedSubGraph &inlined = inlined_map[conn.to_node];
		if (conn.to_pin < 0 || conn.to_pin >= inlined.input_node_ids.size()) {
			result.errors.push_back(vformat("SubGraph node %d: input pin %d out of range (has %d inputs).", conn.to_node, conn.to_pin, inlined.input_node_ids.size()));
			return result;
		}
		int32_t graph_input_node_id = inlined.input_node_ids[conn.to_pin];

		// Find all connections in result.graph.connections that have from_node == graph_input_node_id
		// and replace their from_node/from_pin with the parent source.
		for (ConnectionDesc &internal_conn : result.graph.connections) {
			if (internal_conn.from_node == graph_input_node_id) {
				internal_conn.from_node = conn.from_node;
				internal_conn.from_pin = conn.from_pin;
			}
		}
	}

	// Handle connections FROM SubGraph nodes.
	// For each: SubGraph:pin_N → dest_node:dest_pin
	// Find the connection TO GraphOutput[N] inside the sub-graph, and rewire its source to dest.
	for (const ConnectionDesc &conn : parent_from_subgraph) {
		const InlinedSubGraph &inlined = inlined_map[conn.from_node];
		if (conn.from_pin < 0 || conn.from_pin >= inlined.output_node_ids.size()) {
			result.errors.push_back(vformat("SubGraph node %d: output pin %d out of range (has %d outputs).", conn.from_node, conn.from_pin, inlined.output_node_ids.size()));
			return result;
		}
		int32_t graph_output_node_id = inlined.output_node_ids[conn.from_pin];

		// Find the connection TO graph_output_node_id in result.graph.connections.
		// Its source becomes the source for our new connection to the parent dest.
		bool found = false;
		for (const ConnectionDesc &internal_conn : result.graph.connections) {
			if (internal_conn.to_node == graph_output_node_id) {
				ConnectionDesc new_conn;
				new_conn.from_node = internal_conn.from_node;
				new_conn.from_pin = internal_conn.from_pin;
				new_conn.to_node = conn.to_node;
				new_conn.to_pin = conn.to_pin;
				result.graph.connections.push_back(new_conn);
				found = true;
				break;
			}
		}
		if (!found) {
			result.errors.push_back(vformat("SubGraph node %d: GraphOutput (pin %d) has no incoming connection in sub-graph.", conn.from_node, conn.from_pin));
			return result;
		}
	}

	// Remove GraphInput and GraphOutput nodes from the result (they are pass-throughs now).
	// Also remove connections TO GraphOutput nodes and FROM GraphInput nodes.
	HashSet<int32_t> io_node_ids;
	for (const KeyValue<int32_t, InlinedSubGraph> &kv : inlined_map) {
		for (int32_t id : kv.value.input_node_ids) {
			io_node_ids.insert(id);
		}
		for (int32_t id : kv.value.output_node_ids) {
			io_node_ids.insert(id);
		}
	}

	// Filter out I/O nodes.
	Vector<NodeDesc> filtered_nodes;
	for (const NodeDesc &nd : result.graph.nodes) {
		if (!io_node_ids.has(nd.id)) {
			filtered_nodes.push_back(nd);
		}
	}
	result.graph.nodes = filtered_nodes;

	// Filter out connections referencing I/O nodes.
	Vector<ConnectionDesc> filtered_connections;
	for (const ConnectionDesc &conn : result.graph.connections) {
		if (!io_node_ids.has(conn.from_node) && !io_node_ids.has(conn.to_node)) {
			filtered_connections.push_back(conn);
		}
	}
	result.graph.connections = filtered_connections;

	return result;
}
