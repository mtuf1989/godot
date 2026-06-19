#include "cip_modifier_negate.h"

Variant CIPModifierNegate::modify(const Variant &p_value, double p_delta) const {
	switch (p_value.get_type()) {
		case Variant::FLOAT:
			return -(float)p_value;
		case Variant::VECTOR2:
			return -(Vector2)p_value;
		case Variant::VECTOR3:
			return -(Vector3)p_value;
		default:
			return p_value;
	}
}

void CIPModifierNegate::_bind_methods() {
}
