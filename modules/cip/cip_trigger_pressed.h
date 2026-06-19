#pragma once

#include "cip_trigger.h"

class CIPTriggerPressed : public CIPTrigger {
	GDCLASS(CIPTriggerPressed, CIPTrigger);

	float actuation_threshold = 0.5f;

protected:
	static void _bind_methods();

public:
	void set_actuation_threshold(float p_value);
	float get_actuation_threshold() const;

	virtual TriggerState update_state(const Variant &p_value, double p_delta) override;
};
