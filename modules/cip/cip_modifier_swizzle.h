#pragma once

#include "cip_modifier.h"

class CIPModifierSwizzle : public CIPModifier {
	GDCLASS(CIPModifierSwizzle, CIPModifier);

public:
	enum SwizzleOrder {
		YXZ,
		ZYX,
		XZY,
		YZX,
		ZXY,
	};

private:
	SwizzleOrder order = YXZ;

protected:
	static void _bind_methods();

public:
	void set_order(SwizzleOrder p_order);
	SwizzleOrder get_order() const;

	virtual Variant modify(const Variant &p_value, double p_delta) const override;
};

VARIANT_ENUM_CAST(CIPModifierSwizzle::SwizzleOrder);
