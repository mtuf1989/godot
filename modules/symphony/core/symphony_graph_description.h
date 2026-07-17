#pragma once

#include "core/math/vector2.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

// Describes a single node in the graph (input to the compiler).
struct NodeDesc {
	int32_t id = -1; // Unique node ID within this graph
	StringName type_name; // Must match a registered OperatorDescriptor
	HashMap<StringName, Variant> params; // Operator-specific parameters
	Vector2 editor_position; // Editor-only: node position on GraphEdit canvas
	bool collapsed = false; // Editor-only: whether params are collapsed
};

// Describes a connection between two nodes.
struct ConnectionDesc {
	int32_t from_node = -1; // Source node ID
	int32_t from_pin = 0;   // Output pin index on source node
	int32_t to_node = -1;   // Destination node ID
	int32_t to_pin = 0;     // Input pin index on destination node
	bool is_feedback = false; // If true, compiler inserts 1-block delay (breaks cycle)
};

// Describes a comment frame in the editor.
struct FrameDesc {
	int32_t id = -1;
	String title;
	Vector2 editor_position;
	Vector2 editor_size = Vector2(300, 200);
	Color tint_color = Color(0.3, 0.3, 0.3, 0.75);
	Vector<int32_t> attached_nodes; // Node IDs grouped by this frame
};

// Complete description of a graph to be compiled.
struct GraphDescription {
	Vector<NodeDesc> nodes;
	Vector<ConnectionDesc> connections;
	Vector<FrameDesc> frames;

	// --- Compile-time quality options ---

	// When true, the compiler auto-inserts a 2nd-order lowpass filter (18 kHz, Q=0.707)
	// between consecutive nonlinear operators (those with OperatorDescriptor::nonlinear == true).
	// This prevents harmonic cascade aliasing in chains like Saturator → Waveshaper → Saturator.
	// Default: false (opt-in per graph).
	bool anti_alias_staircase = false;

	// When true, the compiler auto-inserts ParameterSmoother nodes on FLOAT output→FLOAT input
	// connections where the source is a GraphInput node (i.e., runtime-controllable parameters).
	// Prevents clicks/zipper noise when game code changes parameters at 60Hz frame rate.
	// Smoothing time defaults to 5ms (one-pole convergence toward target).
	// Default: true (opt-out per graph). Set false for graphs that need instant parameter response.
	bool smooth_parameters = true;

	// Smoothing time in milliseconds applied by compiler-injected smoothers.
	// Only used when smooth_parameters == true.
	float smooth_time_ms = 5.0f;
};
