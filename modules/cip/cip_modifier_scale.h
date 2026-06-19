#pragma once

#include "cip_modifier.h"

class CIPModifierScale : public CIPModifier {
	GDCLASS(CIPModifierScale, CIPModifier);

	Vector3 scale = Vector3(1, 1, 1);

protected:
	static void _bind_methods();

public:
	void set_scale(const Vector3 &p_scale);
	Vector3 get_scale() const;

	virtual Variant modify(const Variant &p_value, double p_delta) const override;
};
