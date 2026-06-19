#pragma once

#include "cip_action.h"
#include "cip_modifier.h"
#include "cip_trigger.h"
#include "core/input/input_event.h"
#include "core/io/resource.h"
#include "core/variant/typed_array.h"

class CIPActionMapping : public Resource {
	GDCLASS(CIPActionMapping, Resource);

private:
	Ref<CIPAction> action;
	Ref<InputEvent> input_event;
	TypedArray<CIPModifier> modifiers;
	TypedArray<CIPTrigger> triggers;
	bool consume_input = true;

protected:
	static void _bind_methods();

public:
	void set_action(const Ref<CIPAction> &p_action);
	Ref<CIPAction> get_action() const;

	void set_input_event(const Ref<InputEvent> &p_event);
	Ref<InputEvent> get_input_event() const;

	void set_modifiers(const TypedArray<CIPModifier> &p_modifiers);
	TypedArray<CIPModifier> get_modifiers() const;

	void set_triggers(const TypedArray<CIPTrigger> &p_triggers);
	TypedArray<CIPTrigger> get_triggers() const;

	void set_consume_input(bool p_consume);
	bool get_consume_input() const;
};
