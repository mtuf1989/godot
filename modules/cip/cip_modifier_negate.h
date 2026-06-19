#pragma once

#include "cip_modifier.h"

class CIPModifierNegate : public CIPModifier {
	GDCLASS(CIPModifierNegate, CIPModifier);

protected:
	static void _bind_methods();

public:
	virtual Variant modify(const Variant &p_value, double p_delta) const override;
};
