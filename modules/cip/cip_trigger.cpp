#include "cip_trigger.h"

#include "core/object/class_db.h"

CIPTrigger::TriggerState CIPTrigger::update_state(const Variant &p_value, double p_delta) {
	int ret = (int)STATE_NONE;
	GDVIRTUAL_CALL(_update_state, p_value, p_delta, ret);
	return (TriggerState)ret;
}

void CIPTrigger::_bind_methods() {
	ClassDB::bind_method(D_METHOD("update_state", "value", "delta"), &CIPTrigger::update_state);
	GDVIRTUAL_BIND(_update_state, "value", "delta");

	BIND_ENUM_CONSTANT(STATE_NONE);
	BIND_ENUM_CONSTANT(STATE_ONGOING);
	BIND_ENUM_CONSTANT(STATE_TRIGGERED);
}
