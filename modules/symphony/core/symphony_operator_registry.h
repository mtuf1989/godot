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

// Describes an operator type: its pins, state size, and factory function.
struct OperatorDescriptor {
	StringName type_name;
	String category; // For editor menu grouping (e.g., "Generators", "Filters")
	Vector<PinDescriptor> inputs;
	Vector<PinDescriptor> outputs;
	Vector<ParamDescriptor> params; // Editable parameters exposed in the editor
	size_t state_size = 0; // sizeof(ConcreteOperator)
	size_t state_align = 8; // alignof(ConcreteOperator)
	OperatorCreateFunc create_fn = nullptr;
};

// Singleton registry of all known operator types.
class OperatorRegistry {
private:
	static OperatorRegistry *singleton;
	HashMap<StringName, OperatorDescriptor> descriptors;

public:
	static OperatorRegistry *get_singleton();
	static void create_singleton();
	static void destroy_singleton();

	void register_operator(const OperatorDescriptor &p_desc);
	const OperatorDescriptor *find(const StringName &p_type_name) const;
	void get_registered_types(Vector<StringName> &r_types) const;
};
