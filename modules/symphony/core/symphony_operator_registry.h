#pragma once

#include "symphony_pin_types.h"
#include "core/string/string_name.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

class SymphonyOperator;
struct ArenaAllocator;

// Describes a single input or output pin on an operator type.
struct PinDescriptor {
	StringName name;
	SymphonyPinType type = SymphonyPinType::AUDIO;
	bool required = true; // If true, compiler errors when unconnected (inputs only)
};

// Describes an editable parameter on an operator type.
struct ParamDescriptor {
	StringName name;
	float default_value = 0.0f;
	float min_value = -10000.0f;
	float max_value = 10000.0f;
	float step = 0.01f;
};

// Function signature for creating an operator instance via placement new in the arena.
// Returns the operator pointer (which lives inside the arena).
using OperatorCreateFunc = SymphonyOperator *(*)(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate);

// Function signature for per-instance arena sizing.
// Called by the graph compiler with the node's resolved params and mix rate to
// determine how many extra arena bytes this specific instance needs.
// Returns the byte count (excluding state_size which is added separately).
using ExtraArenaBytesFunc = size_t (*)(const HashMap<StringName, Variant> &p_params, float p_mix_rate);

// Describes an operator type: its pins, state size, and factory function.
struct OperatorDescriptor {
	StringName type_name;
	String category; // For editor menu grouping (e.g., "Generators", "Filters")
	Vector<PinDescriptor> inputs;
	Vector<PinDescriptor> outputs;
	Vector<ParamDescriptor> params; // Editable parameters exposed in the editor
	size_t state_size = 0; // sizeof(ConcreteOperator)
	size_t state_align = 8; // alignof(ConcreteOperator)
	size_t extra_arena_bytes = 0; // Additional arena bytes needed by create_fn (e.g., lookup tables)
	ExtraArenaBytesFunc extra_arena_bytes_fn = nullptr; // Per-instance override; if set, takes priority over extra_arena_bytes
	OperatorCreateFunc create_fn = nullptr;
	bool nonlinear = false; // If true, this operator generates harmonics (saturators, waveshapers).
	                        // Used by the graph compiler's anti-alias staircase pass (P1b).
};

// Singleton registry of all known operator types.
class OperatorRegistry {
private:
	static OperatorRegistry *singleton;
	HashMap<StringName, OperatorDescriptor> descriptors;
	HashMap<StringName, StringName> aliases; // deprecated_name → current_name (dev-log #15)

public:
	static OperatorRegistry *get_singleton();
	static void create_singleton();
	static void destroy_singleton();

	void register_operator(const OperatorDescriptor &p_desc);
	void register_alias(const StringName &p_old_name, const StringName &p_current_name);
	const OperatorDescriptor *find(const StringName &p_type_name) const;
	void get_registered_types(Vector<StringName> &r_types) const;
};
