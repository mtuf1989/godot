#include "cip_modifier_swizzle.h"

#include "core/object/class_db.h"

void CIPModifierSwizzle::set_order(SwizzleOrder p_order) {
	order = p_order;
}

CIPModifierSwizzle::SwizzleOrder CIPModifierSwizzle::get_order() const {
	return order;
}

Variant CIPModifierSwizzle::modify(const Variant &p_value, double p_delta) const {
	// Handle 1D → Vector2 promotion (float goes to swizzled axis)
	if (p_value.get_type() == Variant::FLOAT) {
		float v = (float)p_value;
		switch (order) {
			case YXZ:
				return Vector2(0, v); // 1D → Y axis
			default:
				return Vector2(v, 0);
		}
	}

	if (p_value.get_type() == Variant::VECTOR2) {
		Vector2 v = p_value;
		switch (order) {
			case YXZ:
				return Vector2(v.y, v.x);
			default:
				return v;
		}
	}

	if (p_value.get_type() == Variant::VECTOR3) {
		Vector3 v = p_value;
		switch (order) {
			case YXZ:
				return Vector3(v.y, v.x, v.z);
			case ZYX:
				return Vector3(v.z, v.y, v.x);
			case XZY:
				return Vector3(v.x, v.z, v.y);
			case YZX:
				return Vector3(v.y, v.z, v.x);
			case ZXY:
				return Vector3(v.z, v.x, v.y);
		}
	}

	return p_value;
}

void CIPModifierSwizzle::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_order", "order"), &CIPModifierSwizzle::set_order);
	ClassDB::bind_method(D_METHOD("get_order"), &CIPModifierSwizzle::get_order);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "order", PROPERTY_HINT_ENUM, "YXZ,ZYX,XZY,YZX,ZXY"), "set_order", "get_order");

	BIND_ENUM_CONSTANT(YXZ);
	BIND_ENUM_CONSTANT(ZYX);
	BIND_ENUM_CONSTANT(XZY);
	BIND_ENUM_CONSTANT(YZX);
	BIND_ENUM_CONSTANT(ZXY);
}
