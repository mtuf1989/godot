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
	active_contexts.erase(p_context);
}

void CIPSubsystem::clear_mapping_contexts() {
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

void CIPSubsystem::unhandled_input(const Ref<InputEvent> &p_event) {
	double delta = get_process_delta_time();
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
					p_event, false, 0.5f, &pressed, &strength, &raw_strength);

			if (!matched) {
				continue;
			}

			// Run modifier chain
			Variant value = Variant(strength);
			TypedArray<CIPModifier> modifiers = mapping->get_modifiers();
			for (int m = 0; m < modifiers.size(); m++) {
				Ref<CIPModifier> mod = modifiers[m];
				if (mod.is_valid()) {
					value = mod->modify(value, delta);
				}
			}

			// Run trigger evaluation
			CIPTrigger::TriggerState trigger_result = CIPTrigger::STATE_TRIGGERED;
			TypedArray<CIPTrigger> triggers = mapping->get_triggers();
			for (int t = 0; t < triggers.size(); t++) {
				Ref<CIPTrigger> trigger = triggers[t];
				if (trigger.is_valid()) {
					CIPTrigger::TriggerState result = trigger->update_state(value, delta);
					if ((int)result < (int)trigger_result) {
						trigger_result = result;
					}
				}
			}

			// State transition & signal emission
			ActionState &state = action_states[action];
			CIPTrigger::TriggerState prev = state.last_state;
			state.last_state = trigger_result;
			state.last_value = value;

			CIPAction::TriggerEvent event = _compute_trigger_event(prev, trigger_result);
			if (event != CIPAction::TRIGGER_NONE) {
				action->_update(value, event);
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
