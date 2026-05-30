#pragma once

#include "symphony_graph_description.h"
#include "core/string/ustring.h"

// Pre-pass that recursively resolves SubGraph nodes into a flat GraphDescription.
// After flattening, no SubGraph nodes remain — the result can be fed directly
// to GraphCompiler::compile().
class GraphFlattener {
public:
	static constexpr int32_t MAX_RECURSION_DEPTH = 32;

	struct FlattenResult {
		GraphDescription graph;
		Vector<String> errors;
		bool success() const { return errors.is_empty(); }
	};

	// Flatten all SubGraph nodes in p_desc by inlining their referenced graphs.
	// p_owner_path: resource path of the graph being flattened (for cycle detection).
	static FlattenResult flatten(const GraphDescription &p_desc, const String &p_owner_path);
};
