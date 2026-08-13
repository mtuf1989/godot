#pragma once

#include "symphony_graph_description.h"
#include "symphony_compiled_graph.h"
#include "symphony_operator_registry.h"
#include "core/string/ustring.h"

// Compiles a GraphDescription into a CompiledGraph ready for audio-thread execution.
// All work happens on the main thread. The output CompiledGraph is then atomically
// published to the audio thread.
class GraphCompiler {
public:
	struct CompileResult {
		CompiledGraph *graph = nullptr; // Owned by caller. nullptr on failure.
		Vector<String> errors;

		// Exact package accounting (populated on success and on size-calculation failure).
		size_t arena_bytes = 0; // Arena capacity that will be / was allocated
		size_t arena_used_bytes = 0; // Arena bytes consumed after construction (success only)
		size_t non_arena_bytes = 0; // Heap metadata outside the arena (silence tables, etc.)
		size_t trigger_buffer_bytes = 0; // Arena bytes reserved for TriggerBuffer payloads
		size_t route_metadata_bytes = 0; // Non-arena route/silence table bytes
		size_t total_package_bytes = 0; // arena_bytes + non_arena_bytes

		bool success() const { return graph != nullptr; }
	};

	// Compile a graph description into an executable CompiledGraph.
	// p_mix_rate: the audio sample rate (needed by operators for rate-dependent calculations).
	static CompileResult compile(const GraphDescription &p_desc, float p_mix_rate);
};
