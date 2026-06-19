#include "cip_trigger_hold.h"

#include "core/object/class_db.h"

void CIPTriggerHold::set_hold_time(float p_time) {
	hold_time = p_time;
}

float CIPTriggerHold::get_hold_time() const {
	return hold_time;
}

void CIPTriggerHold::set_actuation_threshold(float p_value) {
	actuation_threshold = p_value;
}

float CIPTriggerHold::get_actuation_threshold() const {
	return actuation_threshold;
}

void CIPTriggerHold::set_is_first_trigger(bool p_value) {
	is_first_trigger = p_value;
}

bool CIPTriggerHold::get_is_first_trigger() const {
	return is_first_trigger;
}

static float _get_value_strength_hold(const Variant &p_value) {
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

CIPTrigger::TriggerState CIPTriggerHold::update_state(const Variant &p_value, double p_delta) {
	float strength = _get_value_strength_hold(p_value);

	if (strength >= actuation_threshold) {
		elapsed += p_delta;
		if (elapsed >= hold_time) {
			if (is_first_trigger && has_triggered) {
				return STATE_TRIGGERED; // keep triggered state
			}
			has_triggered = true;
			return STATE_TRIGGERED;
		}
		return STATE_ONGOING;
	}

	elapsed = 0.0f;
	has_triggered = false;
	return STATE_NONE;
}

void CIPTriggerHold::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_hold_time", "time"), &CIPTriggerHold::set_hold_time);
	ClassDB::bind_method(D_METHOD("get_hold_time"), &CIPTriggerHold::get_hold_time);
	ClassDB::bind_method(D_METHOD("set_actuation_threshold", "value"), &CIPTriggerHold::set_actuation_threshold);
	ClassDB::bind_method(D_METHOD("get_actuation_threshold"), &CIPTriggerHold::get_actuation_threshold);
	ClassDB::bind_method(D_METHOD("set_is_first_trigger", "value"), &CIPTriggerHold::set_is_first_trigger);
	ClassDB::bind_method(D_METHOD("get_is_first_trigger"), &CIPTriggerHold::get_is_first_trigger);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hold_time", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_hold_time", "get_hold_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "actuation_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_actuation_threshold", "get_actuation_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_first_trigger"), "set_is_first_trigger", "get_is_first_trigger");
}
