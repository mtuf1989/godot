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

	// Compatibility key for bounded state migration (plan §4/§5).
	// Sorted by node_id for binary search on the audio thread.
	struct OperatorFingerprint {
		int32_t node_id = -1;
		uint32_t type_hash = 0;
		uint32_t structural_hash = 0; // type + exportable state size (layout key)
		int32_t exec_index = -1;
	};

	// Sorted by name for binary search (main-thread control path).
	Vector<ParamRoute> param_routes;
	Vector<TriggerRoute> trigger_routes;
	Vector<OperatorFingerprint> fingerprints;

	size_t arena_bytes = 0;
	size_t total_package_bytes = 0;
	float estimated_cost_units = 0.0f;
	int lod_tier = 0;

	// Intrusive retirement link (filled by retirement queue in a later slice).
	PreparedGraphPackage *retire_next = nullptr;

	static PreparedGraphPackage *create_from_graph(CompiledGraph *p_graph, size_t p_arena_bytes = 0, size_t p_total_bytes = 0, int p_lod_tier = 0, float p_estimated_cost_units = 0.0f);
	static void destroy(PreparedGraphPackage *p_package);

	// Audio-thread: copy ≤256-byte compatible operator state from p_from → p_to.
	// Matches node_id + type_hash + structural_hash; skips larger/incompatible histories.
	static void migrate_compatible_state(const PreparedGraphPackage *p_from, PreparedGraphPackage *p_to);

	[[nodiscard]] SymphonyGraphInput *find_param(const StringName &p_name) const;
	[[nodiscard]] SymphonyTriggerInput *find_trigger(const StringName &p_name) const;
	[[nodiscard]] const OperatorFingerprint *find_fingerprint(int32_t p_node_id) const;
};
