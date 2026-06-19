#include "cip_modifier_scale.h"

#include "core/object/class_db.h"

void CIPModifierScale::set_scale(const Vector3 &p_scale) {
	scale = p_scale;
}

Vector3 CIPModifierScale::get_scale() const {
	return scale;
}

Variant CIPModifierScale::modify(const Variant &p_value, double p_delta) const {
	switch (p_value.get_type()) {
		case Variant::FLOAT:
			return (float)p_value * scale.x;
		case Variant::VECTOR2:
			return Vector2(((Vector2)p_value).x * scale.x, ((Vector2)p_value).y * scale.y);
		case Variant::VECTOR3:
			return (Vector3)p_value * scale;
		default:
			return p_value;
	}
}

void CIPModifierScale::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_scale", "scale"), &CIPModifierScale::set_scale);
	ClassDB::bind_method(D_METHOD("get_scale"), &CIPModifierScale::get_scale);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "scale"), "set_scale", "get_scale");
}
