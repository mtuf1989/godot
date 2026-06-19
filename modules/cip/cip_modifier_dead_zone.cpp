#include "cip_modifier_dead_zone.h"

#include "core/object/class_db.h"

void CIPModifierDeadZone::set_inner_threshold(float p_value) {
	inner_threshold = p_value;
}

float CIPModifierDeadZone::get_inner_threshold() const {
	return inner_threshold;
}

void CIPModifierDeadZone::set_outer_threshold(float p_value) {
	outer_threshold = p_value;
}

float CIPModifierDeadZone::get_outer_threshold() const {
	return outer_threshold;
}

static float _apply_deadzone(float p_value, float p_inner, float p_outer) {
	float abs_val = Math::abs(p_value);
	if (abs_val < p_inner) {
		return 0.0f;
	}
	float range = p_outer - p_inner;
	if (range <= 0.0f) {
		return SIGN(p_value);
	}
	float remapped = CLAMP((abs_val - p_inner) / range, 0.0f, 1.0f);
	return SIGN(p_value) * remapped;
}

Variant CIPModifierDeadZone::modify(const Variant &p_value, double p_delta) const {
	switch (p_value.get_type()) {
		case Variant::FLOAT: {
			return _apply_deadzone((float)p_value, inner_threshold, outer_threshold);
		}
		case Variant::VECTOR2: {
			Vector2 v = p_value;
			float mag = v.length();
			if (mag < inner_threshold) {
				return Vector2();
			}
			float range = outer_threshold - inner_threshold;
			if (range <= 0.0f) {
				return v.normalized();
			}
			float remapped = CLAMP((mag - inner_threshold) / range, 0.0f, 1.0f);
			return v.normalized() * remapped;
		}
		case Variant::VECTOR3: {
			Vector3 v = p_value;
			float mag = v.length();
			if (mag < inner_threshold) {
				return Vector3();
			}
			float range = outer_threshold - inner_threshold;
			if (range <= 0.0f) {
				return v.normalized();
			}
			float remapped = CLAMP((mag - inner_threshold) / range, 0.0f, 1.0f);
			return v.normalized() * remapped;
		}
		default:
			return p_value;
	}
}

void CIPModifierDeadZone::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_inner_threshold", "value"), &CIPModifierDeadZone::set_inner_threshold);
	ClassDB::bind_method(D_METHOD("get_inner_threshold"), &CIPModifierDeadZone::get_inner_threshold);
	ClassDB::bind_method(D_METHOD("set_outer_threshold", "value"), &CIPModifierDeadZone::set_outer_threshold);
	ClassDB::bind_method(D_METHOD("get_outer_threshold"), &CIPModifierDeadZone::get_outer_threshold);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_inner_threshold", "get_inner_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "outer_threshold", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_outer_threshold", "get_outer_threshold");
}
