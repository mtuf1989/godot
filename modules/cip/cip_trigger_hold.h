#pragma once

#include "cip_trigger.h"

class CIPTriggerHold : public CIPTrigger {
	GDCLASS(CIPTriggerHold, CIPTrigger);

	float hold_time = 0.5f;
	float actuation_threshold = 0.5f;
	float elapsed = 0.0f;
	bool is_first_trigger = true;
	bool has_triggered = false;

protected:
	static void _bind_methods();

public:
	void set_hold_time(float p_time);
	float get_hold_time() const;
	void set_actuation_threshold(float p_value);
	float get_actuation_threshold() const;
	void set_is_first_trigger(bool p_value);
	bool get_is_first_trigger() const;

	virtual TriggerState update_state(const Variant &p_value, double p_delta) override;
};
