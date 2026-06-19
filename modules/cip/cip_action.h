#pragma once

#include "core/io/resource.h"

class CIPAction : public Resource {
	GDCLASS(CIPAction, Resource);

public:
	enum ValueType {
		VALUE_BOOL,
		VALUE_FLOAT,
		VALUE_VECTOR2,
		VALUE_VECTOR3
	};

	enum TriggerEvent {
		TRIGGER_NONE,
		TRIGGER_STARTED,
		TRIGGER_ONGOING,
		TRIGGER_TRIGGERED,
		TRIGGER_COMPLETED,
		TRIGGER_CANCELED
	};

private:
	ValueType value_type = VALUE_BOOL;
	Variant current_value;
	TriggerEvent current_state = TRIGGER_NONE;

protected:
	static void _bind_methods();

public:
	void set_value_type(ValueType p_type);
	ValueType get_value_type() const;

	Variant get_value() const;
	TriggerEvent get_trigger_event() const;

	void _update(const Variant &p_value, TriggerEvent p_event);
};

VARIANT_ENUM_CAST(CIPAction::ValueType);
VARIANT_ENUM_CAST(CIPAction::TriggerEvent);
