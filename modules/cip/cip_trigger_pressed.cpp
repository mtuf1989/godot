#include "cip_trigger_pressed.h"

#include "core/object/class_db.h"

void CIPTriggerPressed::set_actuation_threshold(float p_value) {
	actuation_threshold = p_value;
}

float CIPTriggerPressed::get_actuation_threshold() const {
	return actuation_threshold;
}

static float _get_value_strength(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::BOOL:
			return (bool)p_value ? 1.0f : 0.0f;
		case Variant::FLOAT:
			return Math::abs((float)p_value);
		case Variant::VECTOR2:
			return ((Vector2)p_value).length();
		case Variant::VECTOR3:
			return ((Vector3)p_value).length();
		default:
			return 0.0f;
	}
}

CIPTrigger::TriggerState CIPTriggerPressed::update_state(const Variant &p_value, double p_delta) {
	return _get_value_strength(p_value) >= actuation_threshold ? STATE_TRIGGERED : STATE_NONE;
}

void CIPTriggerPressed::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_actuation_threshold", "value"), &CIPTriggerPressed::set_actuation_threshold);
	ClassDB::bind_method(D_METHOD("get_actuation_threshold"), &CIPTriggerPressed::get_actuation_threshold);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "actuation_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_actuation_threshold", "get_actuation_threshold");
}
