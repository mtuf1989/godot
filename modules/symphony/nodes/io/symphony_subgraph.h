#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"

// SubGraph node: references another AudioStreamSymphony resource.
// The GraphFlattener resolves these before compilation — if one reaches
// the compiler, it means flattening was skipped (error).
// Pins are dynamic (determined by the referenced graph's GraphInput/GraphOutput nodes).
class SymphonySubGraph {
public:
	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "SubGraph";
		desc.category = "I/O";
		// No static pins — pins are dynamic based on referenced resource.
		// Params carry the reference path.
		desc.params.push_back({ "resource_path", 0.0f, 0.0f, 0.0f, 0.0f }); // String stored via node params
		desc.state_size = 0;
		desc.state_align = 8;
		desc.create_fn = nullptr; // Cannot be instantiated — flattener must remove before compile
		OperatorRegistry::get_singleton()->register_operator(desc);
	}
};
