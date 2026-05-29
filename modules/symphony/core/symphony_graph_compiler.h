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
		bool success() const { return graph != nullptr; }
	};

	// Compile a graph description into an executable CompiledGraph.
	// p_mix_rate: the audio sample rate (needed by operators for rate-dependent calculations).
	static CompileResult compile(const GraphDescription &p_desc, float p_mix_rate);
};
