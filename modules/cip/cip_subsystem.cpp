#include "cip_subsystem.h"

#include "core/input/input_map.h"
#include "core/object/class_db.h"
#include "scene/main/viewport.h"

CIPSubsystem *CIPSubsystem::singleton = nullptr;

CIPSubsystem *CIPSubsystem::get_singleton() {
	return singleton;
}

CIPSubsystem::CIPSubsystem() {
	if (singleton == nullptr) {
		singleton = this;
	}
}

CIPSubsystem::~CIPSubsystem() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void CIPSubsystem::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			set_process_unhandled_input(true);
			set_process(true);
		} break;
		case NOTIFICATION_PROCESS: {
			// Tick-based trigger evaluation for ongoing actions
			double delta = get_process_delta_time();
			for (KeyValue<Ref<CIPAction>, ActionState> &kv : action_states) {
				if (kv.value.last_state != CIPTrigger::STATE_NONE && kv.value.active_mapping.is_valid()) {
					_evaluate_triggers(kv.key, kv.value, delta);
				}
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			action_states.clear();
		} break;
	}
}

struct CIPContextPriorityComparator {
	bool operator()(const Ref<CIPMappingContext> &a, const Ref<CIPMappingContext> &b) const {
		return a->get_priority() > b->get_priority();
	}
};

void CIPSubsystem::_sort_contexts() {
	active_contexts.sort_custom<CIPContextPriorityComparator>();
}

void CIPSubsystem::add_mapping_context(const Ref<CIPMappingContext> &p_context) {
	if (p_context.is_null() || active_contexts.has(p_context)) {
		return;
	}
	active_contexts.push_back(p_context);
	_sort_contexts();
}

void CIPSubsystem::remove_mapping_context(const Ref<CIPMappingContext> &p_context) {
	if (p_context.is_null()) {
		return;
	}

	// Item 6: Cancel active actions belonging to this context
	TypedArray<CIPActionMapping> mappings = p_context->get_mappings();
	for (int i = 0; i < mappings.size(); i++) {
		Ref<CIPActionMapping> mapping = mappings[i];
		if (mapping.is_null()) {
			continue;
		}
		Ref<CIPAction> action = mapping->get_action();
		if (action.is_valid() && action_states.has(action)) {
			ActionState &state = action_states[action];
			if (state.last_state != CIPTrigger::STATE_NONE) {
				action->_update(state.last_value, CIPAction::TRIGGER_CANCELED);
				state.last_state = CIPTrigger::STATE_NONE;
				state.active_mapping = Ref<CIPActionMapping>();
			}
		}
	}

	active_contexts.erase(p_context);
}

void CIPSubsystem::clear_mapping_contexts() {
	// Cancel all active actions
	for (KeyValue<Ref<CIPAction>, ActionState> &kv : action_states) {
		if (kv.value.last_state != CIPTrigger::STATE_NONE) {
			kv.key->_update(kv.value.last_value, CIPAction::TRIGGER_CANCELED);
		}
	}
	active_contexts.clear();
	action_states.clear();
}

CIPAction::TriggerEvent CIPSubsystem::_compute_trigger_event(
		CIPTrigger::TriggerState p_prev,
		CIPTrigger::TriggerState p_current) {
	if (p_current == CIPTrigger::STATE_TRIGGERED) {
		if (p_prev != CIPTrigger::STATE_TRIGGERED) {
			return CIPAction::TRIGGER_STARTED;
		}
		return CIPAction::TRIGGER_TRIGGERED;
	} else if (p_current == CIPTrigger::STATE_ONGOING) {
		if (p_prev == CIPTrigger::STATE_NONE) {
			return CIPAction::TRIGGER_STARTED;
		}
		return CIPAction::TRIGGER_ONGOING;
	} else {
		if (p_prev == CIPTrigger::STATE_ONGOING) {
			return CIPAction::TRIGGER_CANCELED;
		} else if (p_prev == CIPTrigger::STATE_TRIGGERED) {
			return CIPAction::TRIGGER_COMPLETED;
		}
		return CIPAction::TRIGGER_NONE;
	}
}

void CIPSubsystem::_evaluate_triggers(const Ref<CIPAction> &p_action, ActionState &p_state, double p_delta) {
	Ref<CIPActionMapping> mapping = p_state.active_mapping;
	if (mapping.is_null()) {
		return;
	}

	CIPTrigger::TriggerState trigger_result = CIPTrigger::STATE_TRIGGERED;
	TypedArray<CIPTrigger> triggers = mapping->get_triggers();
	for (int t = 0; t < triggers.size(); t++) {
		Ref<CIPTrigger> trigger = triggers[t];
		if (trigger.is_valid()) {
			CIPTrigger::TriggerState result = trigger->update_state(p_state.last_value, p_delta);
			if ((int)result < (int)trigger_result) {
				trigger_result = result;
			}
		}
	}

	CIPTrigger::TriggerState prev = p_state.last_state;
	p_state.last_state = trigger_result;

	CIPAction::TriggerEvent event = _compute_trigger_event(prev, trigger_result);
	if (event != CIPAction::TRIGGER_NONE) {
		p_action->_update(p_state.last_value, event);
	}

	// If state fell to NONE, clear active mapping
	if (trigger_result == CIPTrigger::STATE_NONE) {
		p_state.active_mapping = Ref<CIPActionMapping>();
	}
}

void CIPSubsystem::unhandled_input(const Ref<InputEvent> &p_event) {
	HashSet<Ref<CIPAction>> consumed_actions;

	for (const Ref<CIPMappingContext> &context : active_contexts) {
		if (context.is_null()) {
			continue;
		}
		TypedArray<CIPActionMapping> mappings = context->get_mappings();

		for (int i = 0; i < mappings.size(); i++) {
			Ref<CIPActionMapping> mapping = mappings[i];
			if (mapping.is_null()) {
				continue;
			}

			Ref<CIPAction> action = mapping->get_action();
			if (action.is_null() || consumed_actions.has(action)) {
				continue;
			}

			Ref<InputEvent> registered = mapping->get_input_event();
			if (registered.is_null()) {
				continue;
			}

			// Device matching
			int reg_device = registered->get_device();
			if (reg_device != InputMap::ALL_DEVICES && reg_device != p_event->get_device()) {
				continue;
			}

			bool pressed = false;
			float strength = 0.0f;
			float raw_strength = 0.0f;

			bool matched = registered->action_match(
					p_event, false, 0.01f, &pressed, &strength, &raw_strength);

			if (!matched) {
				continue;
			}

			// Run modifier chain
			Variant value = Variant(strength);
			TypedArray<CIPModifier> modifiers = mapping->get_modifiers();
			for (int m = 0; m < modifiers.size(); m++) {
				Ref<CIPModifier> mod = modifiers[m];
				if (mod.is_valid()) {
					value = mod->modify(value, 0.0);
				}
			}

			// Store value and active mapping — triggers evaluated in _process
			ActionState &state = action_states[action];
			state.last_value = value;
			state.active_mapping = mapping;

			// For stateless triggers (no triggers assigned), fire immediately
			TypedArray<CIPTrigger> triggers = mapping->get_triggers();
			if (triggers.is_empty()) {
				CIPTrigger::TriggerState prev = state.last_state;
				CIPTrigger::TriggerState current = (strength > 0.0f) ? CIPTrigger::STATE_TRIGGERED : CIPTrigger::STATE_NONE;
				state.last_state = current;

				CIPAction::TriggerEvent event = _compute_trigger_event(prev, current);
				if (event != CIPAction::TRIGGER_NONE) {
					action->_update(value, event);
				}
				if (current == CIPTrigger::STATE_NONE) {
					state.active_mapping = Ref<CIPActionMapping>();
				}
			}

			if (mapping->get_consume_input()) {
				consumed_actions.insert(action);
			}
		}
	}

	if (!consumed_actions.is_empty()) {
		get_viewport()->set_input_as_handled();
	}
}

void CIPSubsystem::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_mapping_context", "context"), &CIPSubsystem::add_mapping_context);
	ClassDB::bind_method(D_METHOD("remove_mapping_context", "context"), &CIPSubsystem::remove_mapping_context);
	ClassDB::bind_method(D_METHOD("clear_mapping_contexts"), &CIPSubsystem::clear_mapping_contexts);
	ClassDB::bind_static_method("CIPSubsystem", D_METHOD("get_singleton"), &CIPSubsystem::get_singleton);
}
