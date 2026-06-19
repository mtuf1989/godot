#include "cip_action.h"

#include "core/object/class_db.h"

void CIPAction::set_value_type(ValueType p_type) {
	value_type = p_type;
}

CIPAction::ValueType CIPAction::get_value_type() const {
	return value_type;
}

Variant CIPAction::get_value() const {
	return current_value;
}

CIPAction::TriggerEvent CIPAction::get_trigger_event() const {
	return current_state;
}

void CIPAction::_update(const Variant &p_value, TriggerEvent p_event) {
	current_value = p_value;
	current_state = p_event;

	switch (p_event) {
		case TRIGGER_STARTED:
			emit_signal(SNAME("started"), p_value);
			break;
		case TRIGGER_ONGOING:
			emit_signal(SNAME("ongoing"), p_value);
			break;
		case TRIGGER_TRIGGERED:
			emit_signal(SNAME("triggered"), p_value);
			break;
		case TRIGGER_COMPLETED:
			emit_signal(SNAME("completed"), p_value);
			break;
		case TRIGGER_CANCELED:
			emit_signal(SNAME("canceled"), p_value);
			break;
		default:
			break;
	}
}

void CIPAction::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_value_type", "type"), &CIPAction::set_value_type);
	ClassDB::bind_method(D_METHOD("get_value_type"), &CIPAction::get_value_type);
	ClassDB::bind_method(D_METHOD("get_value"), &CIPAction::get_value);
	ClassDB::bind_method(D_METHOD("get_trigger_event"), &CIPAction::get_trigger_event);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "value_type", PROPERTY_HINT_ENUM, "Bool,Float,Vector2,Vector3"), "set_value_type", "get_value_type");

	BIND_ENUM_CONSTANT(VALUE_BOOL);
	BIND_ENUM_CONSTANT(VALUE_FLOAT);
	BIND_ENUM_CONSTANT(VALUE_VECTOR2);
	BIND_ENUM_CONSTANT(VALUE_VECTOR3);

	BIND_ENUM_CONSTANT(TRIGGER_NONE);
	BIND_ENUM_CONSTANT(TRIGGER_STARTED);
	BIND_ENUM_CONSTANT(TRIGGER_ONGOING);
	BIND_ENUM_CONSTANT(TRIGGER_TRIGGERED);
	BIND_ENUM_CONSTANT(TRIGGER_COMPLETED);
	BIND_ENUM_CONSTANT(TRIGGER_CANCELED);

	ADD_SIGNAL(MethodInfo("started", PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("ongoing", PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("triggered", PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("completed", PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
	ADD_SIGNAL(MethodInfo("canceled", PropertyInfo(Variant::NIL, "value", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NIL_IS_VARIANT)));
}
