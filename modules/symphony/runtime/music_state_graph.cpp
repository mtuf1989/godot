#include "music_state_graph.h"
#include "core/object/class_db.h"
#include "core/templates/local_vector.h"

void MusicStateGraph::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_states", "states"), &MusicStateGraph::set_states);
	ClassDB::bind_method(D_METHOD("get_states"), &MusicStateGraph::get_states);
	ClassDB::bind_method(D_METHOD("set_transitions", "transitions"), &MusicStateGraph::set_transitions);
	ClassDB::bind_method(D_METHOD("get_transitions"), &MusicStateGraph::get_transitions);
	ClassDB::bind_method(D_METHOD("set_initial_state", "state"), &MusicStateGraph::set_initial_state);
	ClassDB::bind_method(D_METHOD("get_initial_state"), &MusicStateGraph::get_initial_state);

	ClassDB::bind_method(D_METHOD("get_state", "name"), &MusicStateGraph::get_state);
	ClassDB::bind_method(D_METHOD("find_transition", "from", "to"), &MusicStateGraph::find_transition);
	ClassDB::bind_method(D_METHOD("get_state_names"), &MusicStateGraph::get_state_names);
	ClassDB::bind_method(D_METHOD("validate"), &MusicStateGraph::validate);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "states"), "set_states", "get_states");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transitions", PROPERTY_HINT_TYPE_STRING, String::num(Variant::DICTIONARY) + ":"), "set_transitions", "get_transitions");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "initial_state"), "set_initial_state", "get_initial_state");

	BIND_ENUM_CONSTANT(TRANSITION_CROSSFADE);
	BIND_ENUM_CONSTANT(TRANSITION_FADE_THROUGH_SILENCE);
	BIND_ENUM_CONSTANT(TRANSITION_CUT);
	BIND_ENUM_CONSTANT(TRANSITION_STINGER);
	BIND_ENUM_CONSTANT(QUANTIZE_IMMEDIATE);
	BIND_ENUM_CONSTANT(QUANTIZE_NEXT_BEAT);
	BIND_ENUM_CONSTANT(QUANTIZE_NEXT_BAR);
}

void MusicStateGraph::_validate_property(PropertyInfo &p_property) const {
}

Dictionary MusicStateGraph::get_state(const StringName &p_name) const {
	if (states.has(p_name)) {
		return states[p_name];
	}
	return Dictionary();
}

Dictionary MusicStateGraph::find_transition(const StringName &p_from, const StringName &p_to) const {
	// First: exact match
	for (int i = 0; i < transitions.size(); i++) {
		Dictionary t = transitions[i];
		StringName from = t.get("from", StringName());
		StringName to = t.get("to", StringName());
		if (from == p_from && to == p_to) {
			return t;
		}
	}
	// Second: wildcard from
	for (int i = 0; i < transitions.size(); i++) {
		Dictionary t = transitions[i];
		StringName from = t.get("from", StringName());
		StringName to = t.get("to", StringName());
		if (from == StringName("*") && to == p_to) {
			return t;
		}
	}
	// Third: wildcard to
	for (int i = 0; i < transitions.size(); i++) {
		Dictionary t = transitions[i];
		StringName from = t.get("from", StringName());
		StringName to = t.get("to", StringName());
		if (from == p_from && to == StringName("*")) {
			return t;
		}
	}
	// Fourth: wildcard both (default transition)
	for (int i = 0; i < transitions.size(); i++) {
		Dictionary t = transitions[i];
		StringName from = t.get("from", StringName());
		StringName to = t.get("to", StringName());
		if (from == StringName("*") && to == StringName("*")) {
			return t;
		}
	}
	return Dictionary();
}

PackedStringArray MusicStateGraph::get_state_names() const {
	PackedStringArray names;
	LocalVector<Variant> keys = states.get_key_list();
	for (const Variant &k : keys) {
		names.push_back(String(k));
	}
	return names;
}

bool MusicStateGraph::validate() const {
	if (initial_state == StringName() && !states.is_empty()) {
		WARN_PRINT("MusicStateGraph: initial_state is empty.");
		return false;
	}
	if (!initial_state.is_empty() && !states.has(initial_state)) {
		WARN_PRINT(vformat("MusicStateGraph: initial_state '%s' not found in states.", String(initial_state)));
		return false;
	}
	// Validate transitions reference existing states
	for (int i = 0; i < transitions.size(); i++) {
		Dictionary t = transitions[i];
		StringName from = t.get("from", StringName());
		StringName to = t.get("to", StringName());
		if (from != StringName("*") && !states.has(from)) {
			WARN_PRINT(vformat("MusicStateGraph: transition %d references non-existent state '%s'.", i, String(from)));
			return false;
		}
		if (to != StringName("*") && !states.has(to)) {
			WARN_PRINT(vformat("MusicStateGraph: transition %d references non-existent state '%s'.", i, String(to)));
			return false;
		}
	}
	return true;
}
