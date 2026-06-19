#include "cip_modifier.h"

#include "core/object/class_db.h"

Variant CIPModifier::modify(const Variant &p_value, double p_delta) const {
	Variant ret = p_value;
	GDVIRTUAL_CALL(_modify, p_value, p_delta, ret);
	return ret;
}

void CIPModifier::_bind_methods() {
	ClassDB::bind_method(D_METHOD("modify", "value", "delta"), &CIPModifier::modify);
	GDVIRTUAL_BIND(_modify, "value", "delta");
}
