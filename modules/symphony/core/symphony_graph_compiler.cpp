#include "symphony_graph_compiler.h"
#include "symphony_pin_types.h"
#include "symphony_trigger.h"

static size_t pin_buffer_size(SymphonyPinType p_type) {
	switch (p_type) {
		case SymphonyPinType::AUDIO:
			return sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE;
		case SymphonyPinType::FLOAT:
			return sizeof(float);
		case SymphonyPinType::INT:
			return sizeof(int32_t);
		case SymphonyPinType::BOOL:
			return sizeof(bool);
		case SymphonyPinType::TRIGGER:
			return sizeof(TriggerBuffer);
	}
	return 0;
}

GraphCompiler::CompileResult GraphCompiler::compile(const GraphDescription &p_desc, float p_mix_rate) {
	CompileResult result;
	const OperatorRegistry *registry = OperatorRegistry::get_singleton();

	if (!registry) {
		result.errors.push_back("OperatorRegistry not initialized.");
		return result;
	}

	// --- Pre-pass: Anti-alias staircase injection ---
	// If enabled, insert a BiquadFilter (LP, 18kHz, Q=0.707) between consecutive
	// nonlinear operators on audio connections.
	GraphDescription effective_desc;
	if (p_desc.anti_alias_staircase) {
		effective_desc = p_desc; // Copy — we'll add synthetic nodes and rewire connections.

		// Find the max node ID to generate unique IDs for injected nodes.
		int32_t max_id = 0;
		for (int32_t i = 0; i < effective_desc.nodes.size(); i++) {
			if (effective_desc.nodes[i].id > max_id) {
				max_id = effective_desc.nodes[i].id;
			}
		}
		int32_t next_synth_id = max_id + 1000; // Synthetic IDs start well above user range.

		// Build a lookup: node_id → is_nonlinear
		HashMap<int32_t, bool> node_nonlinear;
		for (int32_t i = 0; i < effective_desc.nodes.size(); i++) {
			const NodeDesc &nd = effective_desc.nodes[i];
			const OperatorDescriptor *desc = registry->find(nd.type_name);
			node_nonlinear.insert(nd.id, desc && desc->nonlinear);
		}

		// Scan connections: if an audio connection goes FROM a nonlinear node TO a nonlinear node,
		// inject a BiquadFilter (LP@18kHz) in between.
		int32_t original_conn_count = effective_desc.connections.size();
		for (int32_t c = 0; c < original_conn_count; c++) {
			ConnectionDesc &conn = effective_desc.connections.write[c];
			if (conn.is_feedback) continue; // Don't inject on feedback edges.

			bool from_nonlinear = node_nonlinear.has(conn.from_node) && node_nonlinear[conn.from_node];
			bool to_nonlinear = node_nonlinear.has(conn.to_node) && node_nonlinear[conn.to_node];

			if (!from_nonlinear || !to_nonlinear) continue;

			// Verify it's an audio connection by checking output pin type.
			const NodeDesc *from_nd = nullptr;
			for (int32_t i = 0; i < effective_desc.nodes.size(); i++) {
				if (effective_desc.nodes[i].id == conn.from_node) {
					from_nd = &effective_desc.nodes[i];
					break;
				}
			}
			if (!from_nd) continue;
			const OperatorDescriptor *from_desc = registry->find(from_nd->type_name);
			if (!from_desc) continue;
			if (conn.from_pin >= from_desc->outputs.size()) continue;
			if (from_desc->outputs[conn.from_pin].type != SymphonyPinType::AUDIO) continue;

			// Inject a synthetic BiquadFilter node.
			NodeDesc lpf_node;
			lpf_node.id = next_synth_id++;
			lpf_node.type_name = StringName("BiquadFilter");
			lpf_node.params.insert(StringName("mode"), 0.0f);    // LP
			lpf_node.params.insert(StringName("cutoff"), 18000.0f);
			lpf_node.params.insert(StringName("resonance"), 0.707f);
			effective_desc.nodes.push_back(lpf_node);

			// Rewire: original connection now goes from_node → LPF input (pin 0)
			//         new connection: LPF output (pin 0) → original to_node/to_pin
			int32_t orig_to_node = conn.to_node;
			int32_t orig_to_pin = conn.to_pin;

			conn.to_node = lpf_node.id;
			conn.to_pin = 0; // BiquadFilter input pin 0 = "input"

			ConnectionDesc new_conn;
			new_conn.from_node = lpf_node.id;
			new_conn.from_pin = 0; // BiquadFilter output pin 0 = "output"
			new_conn.to_node = orig_to_node;
			new_conn.to_pin = orig_to_pin;
			new_conn.is_feedback = false;
			effective_desc.connections.push_back(new_conn);
		}
	}

	const GraphDescription &desc_ref = p_desc.anti_alias_staircase ? effective_desc : p_desc;

	int32_t node_count = desc_ref.nodes.size();
	if (node_count == 0) {
		result.errors.push_back("Graph has no nodes.");
		return result;
	}

	// --- Phase 1: Validate node types ---
	// Map node ID -> index in the nodes array for fast lookup.
	HashMap<int32_t, int32_t> id_to_index;
	Vector<const OperatorDescriptor *> node_descs;
	node_descs.resize(node_count);

	for (int32_t i = 0; i < node_count; i++) {
		const NodeDesc &nd = desc_ref.nodes[i];
		if (id_to_index.has(nd.id)) {
			result.errors.push_back(vformat("Duplicate node ID: %d", nd.id));
			return result;
		}
		id_to_index.insert(nd.id, i);

		const OperatorDescriptor *desc = registry->find(nd.type_name);
		if (!desc) {
			result.errors.push_back(vformat("Unknown operator type: '%s' (node %d)", String(nd.type_name), nd.id));
			return result;
		}
		node_descs.write[i] = desc;
	}

	// --- Phase 2: Validate connections and build adjacency ---
	// in_degree[i] = number of incoming edges to node i
	Vector<int32_t> in_degree;
	in_degree.resize(node_count);
	for (int32_t i = 0; i < node_count; i++) {
		in_degree.write[i] = 0;
	}

	// adjacency[i] = list of node indices that depend on node i
	Vector<Vector<int32_t>> adjacency;
	adjacency.resize(node_count);

	// connection_map: for each node index, for each input pin, which (node_idx, output_pin) feeds it
	struct PinSource {
		int32_t source_node_idx = -1;
		int32_t source_pin = -1;
		bool needs_promotion = false; // Float→Audio promotion needed
	};
	Vector<Vector<PinSource>> input_sources; // [node_idx][input_pin_idx]
	input_sources.resize(node_count);
	for (int32_t i = 0; i < node_count; i++) {
		int32_t input_count = node_descs[i]->inputs.size();
		input_sources.write[i].resize(input_count);
		for (int32_t j = 0; j < input_count; j++) {
			input_sources.write[i].write[j] = PinSource();
		}
	}

	for (int32_t c = 0; c < desc_ref.connections.size(); c++) {
		const ConnectionDesc &conn = desc_ref.connections[c];

		if (!id_to_index.has(conn.from_node)) {
			result.errors.push_back(vformat("Connection %d: source node %d not found.", c, conn.from_node));
			return result;
		}
		if (!id_to_index.has(conn.to_node)) {
			result.errors.push_back(vformat("Connection %d: destination node %d not found.", c, conn.to_node));
			return result;
		}

		int32_t from_idx = id_to_index[conn.from_node];
		int32_t to_idx = id_to_index[conn.to_node];

		const OperatorDescriptor *from_desc = node_descs[from_idx];
		const OperatorDescriptor *to_desc = node_descs[to_idx];

		if (conn.from_pin < 0 || conn.from_pin >= from_desc->outputs.size()) {
			result.errors.push_back(vformat("Connection %d: output pin %d out of range on node %d.", c, conn.from_pin, conn.from_node));
			return result;
		}
		if (conn.to_pin < 0 || conn.to_pin >= to_desc->inputs.size()) {
			result.errors.push_back(vformat("Connection %d: input pin %d out of range on node %d.", c, conn.to_pin, conn.to_node));
			return result;
		}

		// Type check (allow Float→Audio implicit promotion)
		SymphonyPinType out_type = from_desc->outputs[conn.from_pin].type;
		SymphonyPinType in_type = to_desc->inputs[conn.to_pin].type;
		bool type_ok = (out_type == in_type) ||
				(out_type == SymphonyPinType::FLOAT && in_type == SymphonyPinType::AUDIO);
		if (!type_ok) {
			result.errors.push_back(vformat("Connection %d: type mismatch (output pin type %d != input pin type %d).", c, (int)out_type, (int)in_type));
			return result;
		}

		// Record the connection
		bool promotion = (out_type == SymphonyPinType::FLOAT && in_type == SymphonyPinType::AUDIO);

		// Warn if this input pin already has a connection — last one wins, earlier is lost (dev-log #14).
		if (input_sources[to_idx][conn.to_pin].source_node_idx != -1) {
			WARN_PRINT(vformat("Symphony compile: Node %d ('%s') input pin %d ('%s') has multiple connections. "
					"Only the last connection is used. Use a Mix or MathAdd node to combine signals.",
					conn.to_node, String(to_desc->type_name), conn.to_pin,
					String(to_desc->inputs[conn.to_pin].name)));
		}

		input_sources.write[to_idx].write[conn.to_pin] = { from_idx, conn.from_pin, promotion };

		// Build adjacency for topological sort — skip feedback edges (they don't create dependencies)
		if (!conn.is_feedback) {
			adjacency.write[from_idx].push_back(to_idx);
			in_degree.write[to_idx]++;
		}
	}

	// --- Phase 3: Check required inputs ---
	for (int32_t i = 0; i < node_count; i++) {
		const OperatorDescriptor *desc = node_descs[i];
		for (int32_t p = 0; p < desc->inputs.size(); p++) {
			if (desc->inputs[p].required && input_sources[i][p].source_node_idx == -1) {
				result.errors.push_back(vformat("Node %d ('%s'): required input '%s' (pin %d) is not connected.",
						desc_ref.nodes[i].id, String(desc->type_name), String(desc->inputs[p].name), p));
				return result;
			}
		}
	}

	// --- Phase 4: Topological sort (Kahn's algorithm) ---
	Vector<int32_t> sorted_order;
	sorted_order.resize(node_count);
	int32_t sorted_count = 0;

	// Queue of nodes with in_degree == 0
	Vector<int32_t> queue;
	for (int32_t i = 0; i < node_count; i++) {
		if (in_degree[i] == 0) {
			queue.push_back(i);
		}
	}

	while (queue.size() > 0) {
		int32_t node_idx = queue[queue.size() - 1];
		queue.resize(queue.size() - 1);

		sorted_order.write[sorted_count++] = node_idx;

		for (int32_t neighbor : adjacency[node_idx]) {
			in_degree.write[neighbor]--;
			if (in_degree[neighbor] == 0) {
				queue.push_back(neighbor);
			}
		}
	}

	if (sorted_count != node_count) {
		result.errors.push_back("Graph contains a cycle. Mark the feedback edge with is_feedback=true.");
		return result;
	}

	// --- Phase 5: Calculate arena size ---
	size_t arena_size = 0;

	// Space for operator pointer array
	arena_size += sizeof(SymphonyOperator *) * node_count + 32;

	// Space for each operator's state
	for (int32_t i = 0; i < node_count; i++) {
		arena_size += node_descs[i]->state_size + node_descs[i]->state_align;
		// Per-instance sizing callback takes priority over static worst-case value.
		if (node_descs[i]->extra_arena_bytes_fn) {
			arena_size += node_descs[i]->extra_arena_bytes_fn(desc_ref.nodes[i].params);
		} else {
			arena_size += node_descs[i]->extra_arena_bytes;
		}
	}

	// Space for output buffers (one per output pin)
	int32_t total_trigger_buffers = 0;
	for (int32_t i = 0; i < node_count; i++) {
		for (int32_t p = 0; p < node_descs[i]->outputs.size(); p++) {
			arena_size += pin_buffer_size(node_descs[i]->outputs[p].type) + 32;
			if (node_descs[i]->outputs[p].type == SymphonyPinType::TRIGGER) {
				total_trigger_buffers++;
			}
		}
	}

	// Space for trigger buffer pointer array
	arena_size += sizeof(TriggerBuffer *) * total_trigger_buffers + 32;

	// Count Float→Audio promotions needed
	int32_t total_promotions = 0;
	for (int32_t i = 0; i < node_count; i++) {
		for (int32_t p = 0; p < input_sources[i].size(); p++) {
			if (input_sources[i][p].needs_promotion) {
				total_promotions++;
			}
		}
	}
	// Space for promotion buffers (64 floats each) and promotion array
	arena_size += total_promotions * (sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE + 32);
	arena_size += sizeof(CompiledGraph::Promotion) * total_promotions + 32;

	// --- Phase 6: Allocate arena and build compiled graph ---
	// Add safety margin: some operators allocate extra buffers (lookup tables, delay
	// lines, ring buffers) in their create_fn beyond what state_size declares.
	// extra_arena_bytes covers declared needs; this margin handles alignment overhead.
	arena_size += arena_size / 4; // 25% headroom for alignment padding

	CompiledGraph *compiled = memnew(CompiledGraph);
	if (!compiled->arena.init(arena_size)) {
		result.errors.push_back("Failed to allocate arena.");
		memdelete(compiled);
		return result;
	}

	// Allocate operator pointer array
	compiled->operators = (SymphonyOperator **)compiled->arena.alloc(sizeof(SymphonyOperator *) * node_count, 8);
	compiled->operator_count = node_count;

	// Allocate trigger buffer pointer array
	compiled->trigger_buffers = (TriggerBuffer **)compiled->arena.alloc(sizeof(TriggerBuffer *) * total_trigger_buffers, 8);
	compiled->trigger_buffer_count = total_trigger_buffers;

	// Allocate promotion array
	compiled->promotions = (CompiledGraph::Promotion *)compiled->arena.alloc(sizeof(CompiledGraph::Promotion) * total_promotions, 8);
	compiled->promotion_count = total_promotions;
	int32_t promotion_idx = 0;

	// Allocate output buffers for each node (indexed by [node_idx][pin_idx])
	Vector<Vector<void *>> output_buffers;
	output_buffers.resize(node_count);

	int32_t trigger_buf_idx = 0;
	for (int32_t i = 0; i < node_count; i++) {
		int32_t out_count = node_descs[i]->outputs.size();
		output_buffers.write[i].resize(out_count);
		for (int32_t p = 0; p < out_count; p++) {
			SymphonyPinType type = node_descs[i]->outputs[p].type;
			void *buf = compiled->arena.alloc(pin_buffer_size(type), 32);
			output_buffers.write[i].write[p] = buf;
			if (type == SymphonyPinType::TRIGGER) {
				compiled->trigger_buffers[trigger_buf_idx++] = (TriggerBuffer *)buf;
			}
		}
	}

	// --- Phase 7: Create operators and bind pins (in topological order) ---
	// Allocate node_names and node_ids arrays (heap, freed by CompiledGraph destructor).
	compiled->node_names = memnew_arr(StringName, node_count);
	compiled->node_ids = memnew_arr(int32_t, node_count);

	for (int32_t s = 0; s < node_count; s++) {
		int32_t node_idx = sorted_order[s];
		const NodeDesc &nd = desc_ref.nodes[node_idx];
		const OperatorDescriptor *desc = node_descs[node_idx];

		// Create operator via factory function (placement new inside arena)
		SymphonyOperator *op = desc->create_fn(compiled->arena, nd.params, p_mix_rate);
		if (!op) {
			result.errors.push_back(vformat("Failed to create operator '%s' (node %d).", String(desc->type_name), nd.id));
			memdelete(compiled);
			return result;
		}
		compiled->operators[s] = op;

		// Store the node ID for state migration matching.
		compiled->node_ids[s] = nd.id;

		// Store the routing name for GraphInput/TriggerInput nodes.
		if (nd.type_name == StringName("GraphInput") && nd.params.has("parameter_name")) {
			compiled->node_names[s] = StringName(String(nd.params["parameter_name"]));
		} else if (nd.type_name == StringName("TriggerInput") && nd.params.has("trigger_name")) {
			compiled->node_names[s] = StringName(String(nd.params["trigger_name"]));
		} else {
			compiled->node_names[s] = StringName();
		}

		// Build input pointer array
		int32_t input_count = desc->inputs.size();
		int32_t output_count = desc->outputs.size();

		// Temporary arrays on stack (small, bounded by pin count)
		void *input_ptrs[32] = {};
		void *output_ptrs[32] = {};

		for (int32_t p = 0; p < input_count && p < 32; p++) {
			const PinSource &src = input_sources[node_idx][p];
			if (src.source_node_idx >= 0) {
				if (src.needs_promotion) {
					// Allocate a 64-float promotion buffer and register the promotion.
					float *promo_buf = (float *)compiled->arena.alloc(sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE, 32);
					compiled->promotions[promotion_idx].src = (const float *)output_buffers[src.source_node_idx][src.source_pin];
					compiled->promotions[promotion_idx].dst = promo_buf;
					promotion_idx++;
					input_ptrs[p] = promo_buf;
				} else {
					input_ptrs[p] = output_buffers[src.source_node_idx][src.source_pin];
				}
			} else {
				input_ptrs[p] = nullptr; // Unconnected optional input
			}
		}

		for (int32_t p = 0; p < output_count && p < 32; p++) {
			output_ptrs[p] = output_buffers[node_idx][p];
		}

		op->bind_pins(input_ptrs, output_ptrs);
	}

	// --- Phase 8: Build silence propagation tables ---
	// For each operator in execution order, record which other operators (by execution
	// index) feed AUDIO data into it. This enables the execute() loop to skip operators
	// whose audio inputs are all inactive (silent).

	// Map from original node_idx → execution order index (sorted position).
	HashMap<int32_t, int32_t> node_idx_to_exec_order;
	for (int32_t s = 0; s < node_count; s++) {
		node_idx_to_exec_order.insert(sorted_order[s], s);
	}

	// Mark operators as non-skippable based on category.
	// Generators, IO nodes, and timing nodes produce output regardless of input.
	for (int32_t s = 0; s < node_count; s++) {
		int32_t node_idx = sorted_order[s];
		const OperatorDescriptor *desc = node_descs[node_idx];
		String cat = desc->category;
		// Non-skippable categories: they produce output without depending on audio input.
		if (cat == "Generators" || cat == "IO" || cat == "Timing" || cat == "Synthesis") {
			compiled->operators[s]->skippable = 0;
		}
		// Also never skip if the operator has NO audio inputs (it's a source).
		bool has_audio_input = false;
		for (int32_t p = 0; p < desc->inputs.size(); p++) {
			if (desc->inputs[p].type == SymphonyPinType::AUDIO) {
				has_audio_input = true;
				break;
			}
		}
		if (!has_audio_input) {
			compiled->operators[s]->skippable = 0;
		}
	}

	// Build the flat audio dependency array.
	// First pass: count dependencies per operator.
	compiled->audio_input_offsets = memnew_arr(int32_t, node_count + 1);
	compiled->audio_input_offsets[0] = 0;

	for (int32_t s = 0; s < node_count; s++) {
		int32_t node_idx = sorted_order[s];
		const OperatorDescriptor *desc = node_descs[node_idx];
		int32_t audio_dep_count = 0;

		for (int32_t p = 0; p < desc->inputs.size(); p++) {
			if (desc->inputs[p].type == SymphonyPinType::AUDIO || input_sources[node_idx][p].needs_promotion) {
				const PinSource &src = input_sources[node_idx][p];
				if (src.source_node_idx >= 0 && node_idx_to_exec_order.has(src.source_node_idx)) {
					audio_dep_count++;
				}
			}
		}
		compiled->audio_input_offsets[s + 1] = compiled->audio_input_offsets[s] + audio_dep_count;
	}

	compiled->audio_input_ops_count = compiled->audio_input_offsets[node_count];
	if (compiled->audio_input_ops_count > 0) {
		compiled->audio_input_ops = memnew_arr(int32_t, compiled->audio_input_ops_count);

		// Second pass: fill the dependency array.
		int32_t write_idx = 0;
		for (int32_t s = 0; s < node_count; s++) {
			int32_t node_idx = sorted_order[s];
			const OperatorDescriptor *desc = node_descs[node_idx];

			for (int32_t p = 0; p < desc->inputs.size(); p++) {
				if (desc->inputs[p].type == SymphonyPinType::AUDIO || input_sources[node_idx][p].needs_promotion) {
					const PinSource &src = input_sources[node_idx][p];
					if (src.source_node_idx >= 0 && node_idx_to_exec_order.has(src.source_node_idx)) {
						compiled->audio_input_ops[write_idx++] = node_idx_to_exec_order[src.source_node_idx];
					}
				}
			}
		}
	}

	// Build the output audio buffer table for zeroing skipped operators.
	compiled->output_buffer_offsets = memnew_arr(int32_t, node_count + 1);
	compiled->output_buffer_offsets[0] = 0;

	for (int32_t s = 0; s < node_count; s++) {
		int32_t node_idx = sorted_order[s];
		const OperatorDescriptor *desc = node_descs[node_idx];
		int32_t audio_out_count = 0;
		for (int32_t p = 0; p < desc->outputs.size(); p++) {
			if (desc->outputs[p].type == SymphonyPinType::AUDIO) {
				audio_out_count++;
			}
		}
		compiled->output_buffer_offsets[s + 1] = compiled->output_buffer_offsets[s] + audio_out_count;
	}

	compiled->output_audio_buffers_count = compiled->output_buffer_offsets[node_count];
	if (compiled->output_audio_buffers_count > 0) {
		compiled->output_audio_buffers = memnew_arr(float *, compiled->output_audio_buffers_count);

		int32_t buf_write_idx = 0;
		for (int32_t s = 0; s < node_count; s++) {
			int32_t node_idx = sorted_order[s];
			const OperatorDescriptor *desc = node_descs[node_idx];
			for (int32_t p = 0; p < desc->outputs.size(); p++) {
				if (desc->outputs[p].type == SymphonyPinType::AUDIO) {
					compiled->output_audio_buffers[buf_write_idx++] = (float *)output_buffers[node_idx][p];
				}
			}
		}
	}

	result.graph = compiled;
	return result;
}
