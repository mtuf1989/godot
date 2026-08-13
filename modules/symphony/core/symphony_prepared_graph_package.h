/**************************************************************************/
/*  symphony_prepared_graph_package.h                                     */
/**************************************************************************/

#pragma once

#include "symphony_compiled_graph.h"
#include "../nodes/io/symphony_graph_output.h"
#include "../nodes/io/symphony_graph_input.h"
#include "../nodes/io/symphony_trigger_input.h"

#include "core/string/string_name.h"
#include "core/templates/vector.h"

#include <cstdint>

// Immutable playback unit published from the main thread and adopted on audio.
// Built after compile; audio never rebuilds HashMaps or runs dynamic_cast.
struct PreparedGraphPackage {
	CompiledGraph *graph = nullptr;
	SymphonyGraphOutput *graph_output = nullptr;

	struct ParamRoute {
		StringName name;
		SymphonyGraphInput *input = nullptr;
	};
	struct TriggerRoute {
		StringName name;
		SymphonyTriggerInput *input = nullptr;
	};

	// Sorted by name for binary search (main-thread control path).
	Vector<ParamRoute> param_routes;
	Vector<TriggerRoute> trigger_routes;

	size_t arena_bytes = 0;
	size_t total_package_bytes = 0;
	int lod_tier = 0;

	// Intrusive retirement link (filled by retirement queue in a later slice).
	PreparedGraphPackage *retire_next = nullptr;

	static PreparedGraphPackage *create_from_graph(CompiledGraph *p_graph, size_t p_arena_bytes = 0, size_t p_total_bytes = 0, int p_lod_tier = 0);
	static void destroy(PreparedGraphPackage *p_package);

	[[nodiscard]] SymphonyGraphInput *find_param(const StringName &p_name) const;
	[[nodiscard]] SymphonyTriggerInput *find_trigger(const StringName &p_name) const;
};
