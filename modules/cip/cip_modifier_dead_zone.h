#pragma once

#include "cip_modifier.h"

class CIPModifierDeadZone : public CIPModifier {
	GDCLASS(CIPModifierDeadZone, CIPModifier);

	float inner_threshold = 0.2f;
	float outer_threshold = 1.0f;

protected:
	static void _bind_methods();

public:
	void set_inner_threshold(float p_value);
	float get_inner_threshold() const;
	void set_outer_threshold(float p_value);
	float get_outer_threshold() const;

	virtual Variant modify(const Variant &p_value, double p_delta) const override;
};
