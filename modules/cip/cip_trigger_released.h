#pragma once

#include "cip_trigger.h"

class CIPTriggerReleased : public CIPTrigger {
	GDCLASS(CIPTriggerReleased, CIPTrigger);

	float actuation_threshold = 0.5f;
	bool was_pressed = false;

protected:
	static void _bind_methods();

public:
	void set_actuation_threshold(float p_value);
	float get_actuation_threshold() const;

	virtual TriggerState update_state(const Variant &p_value, double p_delta) override;
};
