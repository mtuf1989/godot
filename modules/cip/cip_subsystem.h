#pragma once

#include "cip_mapping_context.h"
#include "cip_trigger.h"
#include "scene/main/node.h"

class CIPSubsystem : public Node {
	GDCLASS(CIPSubsystem, Node);

private:
	static CIPSubsystem *singleton;
	Vector<Ref<CIPMappingContext>> active_contexts;

	struct ActionState {
		CIPTrigger::TriggerState last_state = CIPTrigger::STATE_NONE;
		Variant last_value;
		Ref<CIPActionMapping> active_mapping; // which mapping last fed this action
	};
	HashMap<Ref<CIPAction>, ActionState> action_states;

	void _sort_contexts();
	CIPAction::TriggerEvent _compute_trigger_event(CIPTrigger::TriggerState p_prev, CIPTrigger::TriggerState p_current);
	void _evaluate_triggers(const Ref<CIPAction> &p_action, ActionState &p_state, double p_delta);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	static CIPSubsystem *get_singleton();

	void add_mapping_context(const Ref<CIPMappingContext> &p_context);
	void remove_mapping_context(const Ref<CIPMappingContext> &p_context);
	void clear_mapping_contexts();

	virtual void unhandled_input(const Ref<InputEvent> &p_event) override;

	CIPSubsystem();
	~CIPSubsystem();
};
