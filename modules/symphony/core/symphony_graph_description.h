#pragma once

#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

// Describes a single node in the graph (input to the compiler).
struct NodeDesc {
	int32_t id = -1; // Unique node ID within this graph
	StringName type_name; // Must match a registered OperatorDescriptor
	HashMap<StringName, Variant> params; // Operator-specific parameters
};

// Describes a connection between two nodes.
struct ConnectionDesc {
	int32_t from_node = -1; // Source node ID
	int32_t from_pin = 0;   // Output pin index on source node
	int32_t to_node = -1;   // Destination node ID
	int32_t to_pin = 0;     // Input pin index on destination node
};

// Complete description of a graph to be compiled.
struct GraphDescription {
	Vector<NodeDesc> nodes;
	Vector<ConnectionDesc> connections;
};
