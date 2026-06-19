#include "cip_action_mapping.h"

#include "core/object/class_db.h"

void CIPActionMapping::set_action(const Ref<CIPAction> &p_action) {
	action = p_action;
}

Ref<CIPAction> CIPActionMapping::get_action() const {
	return action;
}

void CIPActionMapping::set_input_event(const Ref<InputEvent> &p_event) {
	input_event = p_event;
}

Ref<InputEvent> CIPActionMapping::get_input_event() const {
	return input_event;
}

void CIPActionMapping::set_modifiers(const TypedArray<CIPModifier> &p_modifiers) {
	modifiers = p_modifiers;
}

TypedArray<CIPModifier> CIPActionMapping::get_modifiers() const {
	return modifiers;
}

void CIPActionMapping::set_triggers(const TypedArray<CIPTrigger> &p_triggers) {
	triggers = p_triggers;
}

TypedArray<CIPTrigger> CIPActionMapping::get_triggers() const {
	return triggers;
}

void CIPActionMapping::set_consume_input(bool p_consume) {
	consume_input = p_consume;
}

bool CIPActionMapping::get_consume_input() const {
	return consume_input;
}

void CIPActionMapping::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_action", "action"), &CIPActionMapping::set_action);
	ClassDB::bind_method(D_METHOD("get_action"), &CIPActionMapping::get_action);
	ClassDB::bind_method(D_METHOD("set_input_event", "event"), &CIPActionMapping::set_input_event);
	ClassDB::bind_method(D_METHOD("get_input_event"), &CIPActionMapping::get_input_event);
	ClassDB::bind_method(D_METHOD("set_modifiers", "modifiers"), &CIPActionMapping::set_modifiers);
	ClassDB::bind_method(D_METHOD("get_modifiers"), &CIPActionMapping::get_modifiers);
	ClassDB::bind_method(D_METHOD("set_triggers", "triggers"), &CIPActionMapping::set_triggers);
	ClassDB::bind_method(D_METHOD("get_triggers"), &CIPActionMapping::get_triggers);
	ClassDB::bind_method(D_METHOD("set_consume_input", "consume"), &CIPActionMapping::set_consume_input);
	ClassDB::bind_method(D_METHOD("get_consume_input"), &CIPActionMapping::get_consume_input);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "action", PROPERTY_HINT_RESOURCE_TYPE, "CIPAction"), "set_action", "get_action");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "input_event", PROPERTY_HINT_RESOURCE_TYPE, "InputEvent"), "set_input_event", "get_input_event");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "modifiers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("CIPModifier")), "set_modifiers", "get_modifiers");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "triggers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("CIPTrigger")), "set_triggers", "get_triggers");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "consume_input"), "set_consume_input", "get_consume_input");
}
