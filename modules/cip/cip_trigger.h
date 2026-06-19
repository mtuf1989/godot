#pragma once

#include "core/io/resource.h"

class CIPTrigger : public Resource {
	GDCLASS(CIPTrigger, Resource);

public:
	enum TriggerState {
		STATE_NONE,
		STATE_ONGOING,
		STATE_TRIGGERED
	};

protected:
	static void _bind_methods();
	GDVIRTUAL2RC(int, _update_state, Variant, double)

public:
	virtual TriggerState update_state(const Variant &p_value, double p_delta);
};

VARIANT_ENUM_CAST(CIPTrigger::TriggerState);
