#include "music_state_graph.h"
#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "core/math/math_funcs.h"

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

	// Coherence API
	ClassDB::bind_method(D_METHOD("validate_musical_coherence"), &MusicStateGraph::validate_musical_coherence);
	ClassDB::bind_method(D_METHOD("is_musically_coherent"), &MusicStateGraph::is_musically_coherent);
	ClassDB::bind_method(D_METHOD("get_transition_coherence_score", "from", "to"), &MusicStateGraph::get_transition_coherence_score);

	// Coherence threshold configuration
	ClassDB::bind_method(D_METHOD("set_coherence_max_energy_delta", "delta"), &MusicStateGraph::set_coherence_max_energy_delta);
	ClassDB::bind_method(D_METHOD("get_coherence_max_energy_delta"), &MusicStateGraph::get_coherence_max_energy_delta);
	ClassDB::bind_method(D_METHOD("set_coherence_max_tempo_ratio", "ratio"), &MusicStateGraph::set_coherence_max_tempo_ratio);
	ClassDB::bind_method(D_METHOD("get_coherence_max_tempo_ratio"), &MusicStateGraph::get_coherence_max_tempo_ratio);
	ClassDB::bind_method(D_METHOD("set_coherence_max_centroid_ratio", "ratio"), &MusicStateGraph::set_coherence_max_centroid_ratio);
	ClassDB::bind_method(D_METHOD("get_coherence_max_centroid_ratio"), &MusicStateGraph::get_coherence_max_centroid_ratio);
	ClassDB::bind_method(D_METHOD("set_coherence_max_key_distance", "distance"), &MusicStateGraph::set_coherence_max_key_distance);
	ClassDB::bind_method(D_METHOD("get_coherence_max_key_distance"), &MusicStateGraph::get_coherence_max_key_distance);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "states"), "set_states", "get_states");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "transitions", PROPERTY_HINT_TYPE_STRING, String::num(Variant::DICTIONARY) + ":"), "set_transitions", "get_transitions");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "initial_state"), "set_initial_state", "get_initial_state");

	ADD_GROUP("Coherence Thresholds", "coherence_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coherence_max_energy_delta", PROPERTY_HINT_RANGE, "0.1,1.0,0.05"), "set_coherence_max_energy_delta", "get_coherence_max_energy_delta");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coherence_max_tempo_ratio", PROPERTY_HINT_RANGE, "1.05,2.0,0.05"), "set_coherence_max_tempo_ratio", "get_coherence_max_tempo_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "coherence_max_centroid_ratio", PROPERTY_HINT_RANGE, "1.2,4.0,0.1"), "set_coherence_max_centroid_ratio", "get_coherence_max_centroid_ratio");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "coherence_max_key_distance", PROPERTY_HINT_RANGE, "1,6,1"), "set_coherence_max_key_distance", "get_coherence_max_key_distance");

	BIND_ENUM_CONSTANT(TRANSITION_CROSSFADE);
	BIND_ENUM_CONSTANT(TRANSITION_FADE_THROUGH_SILENCE);
	BIND_ENUM_CONSTANT(TRANSITION_CUT);
	BIND_ENUM_CONSTANT(TRANSITION_STINGER);
	BIND_ENUM_CONSTANT(QUANTIZE_IMMEDIATE);
	BIND_ENUM_CONSTANT(QUANTIZE_NEXT_BEAT);
	BIND_ENUM_CONSTANT(QUANTIZE_NEXT_BAR);

	BIND_ENUM_CONSTANT(COHERENCE_OK);
	BIND_ENUM_CONSTANT(COHERENCE_WARNING);
	BIND_ENUM_CONSTANT(COHERENCE_ERROR);
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

// ============================================================================
// Musical Key Utilities
// ============================================================================

// Converts a key string (e.g. "C", "Am", "F#", "Bbm") to a semitone value 0-11.
// Minor keys return their root semitone (not the relative major).
int MusicStateGraph::_key_to_semitone(const String &p_key) {
	if (p_key.is_empty()) {
		return -1;
	}

	// Note name to semitone mapping (C=0, C#=1, D=2, ... B=11)
	static const char *note_names[] = { "C", "D", "E", "F", "G", "A", "B" };
	static const int note_semitones[] = { 0, 2, 4, 5, 7, 9, 11 };

	String key_upper = p_key.strip_edges();
	if (key_upper.is_empty()) {
		return -1;
	}

	// Parse root note (first character)
	char32_t root_char = key_upper[0];
	int base_semitone = -1;
	for (int i = 0; i < 7; i++) {
		if (root_char == note_names[i][0]) {
			base_semitone = note_semitones[i];
			break;
		}
	}
	// Try lowercase
	if (base_semitone == -1) {
		char32_t upper = root_char >= 'a' && root_char <= 'g' ? root_char - 32 : root_char;
		for (int i = 0; i < 7; i++) {
			if (upper == note_names[i][0]) {
				base_semitone = note_semitones[i];
				break;
			}
		}
	}
	if (base_semitone == -1) {
		return -1; // Unrecognized note
	}

	// Parse accidentals (# or b, after the root note, before 'm')
	int pos = 1;
	while (pos < key_upper.length()) {
		char32_t c = key_upper[pos];
		if (c == '#') {
			base_semitone = (base_semitone + 1) % 12;
			pos++;
		} else if (c == 'b' && pos > 1) {
			// 'b' as flat only if it's not the root note itself (handled above)
			base_semitone = (base_semitone + 11) % 12;
			pos++;
		} else {
			break;
		}
	}

	return base_semitone;
}

// Computes the minimum distance on the circle of fifths between two semitones.
// Range: 0 (same key / enharmonic) to 6 (tritone / maximally distant).
int MusicStateGraph::_key_distance_fifths(int p_semi_a, int p_semi_b) {
	if (p_semi_a < 0 || p_semi_b < 0) {
		return 0; // Can't compute, treat as no issue
	}

	// Circle of fifths: each step is +7 semitones (mod 12).
	// Position on CoF = how many fifths from C.
	// Invert: semitone_to_cof[s] = (s * 7) % 12
	int cof_a = (p_semi_a * 7) % 12;
	int cof_b = (p_semi_b * 7) % 12;

	int dist = Math::absi(cof_a - cof_b);
	return MIN(dist, 12 - dist); // Minimum wraparound distance
}

// Checks if two keys are relative major/minor (e.g. C and Am, G and Em).
bool MusicStateGraph::_is_relative_key(const String &p_key_a, const String &p_key_b) {
	bool a_minor = p_key_a.ends_with("m") || p_key_a.ends_with("min");
	bool b_minor = p_key_b.ends_with("m") || p_key_b.ends_with("min");

	if (a_minor == b_minor) {
		return false; // Both major or both minor — not a relative pair
	}

	int semi_a = _key_to_semitone(p_key_a);
	int semi_b = _key_to_semitone(p_key_b);
	if (semi_a < 0 || semi_b < 0) {
		return false;
	}

	// Relative minor is 3 semitones below the major (or 9 above, mod 12).
	if (a_minor) {
		// a is minor, b is major. Check if a_root + 3 == b_root (mod 12).
		return ((semi_a + 3) % 12) == semi_b;
	} else {
		// b is minor, a is major. Check if b_root + 3 == a_root (mod 12).
		return ((semi_b + 3) % 12) == semi_a;
	}
}

// ============================================================================
// Musical Coherence Validation (Item 2)
// ============================================================================

TypedArray<Dictionary> MusicStateGraph::validate_musical_coherence() const {
	TypedArray<Dictionary> diagnostics;

	for (int i = 0; i < transitions.size(); i++) {
		Dictionary t = transitions[i];
		StringName from_name = t.get("from", StringName());
		StringName to_name = t.get("to", StringName());

		// Skip wildcard transitions — can't evaluate coherence without concrete states.
		if (from_name == StringName("*") || to_name == StringName("*")) {
			continue;
		}

		// Skip if coherence_override is set on this transition.
		if ((bool)t.get("coherence_override", false)) {
			continue;
		}

		// Get state data
		if (!states.has(from_name) || !states.has(to_name)) {
			continue; // Structural validation handles missing states
		}

		Dictionary from_state = states[from_name];
		Dictionary to_state = states[to_name];

		int trans_type = (int)t.get("type", 0);
		float duration = (float)t.get("duration", 1.0f);
		float pair_score = 1.0f;

		// --- Check 1: Energy Delta ---
		bool has_energy_from = from_state.has("energy") || from_state.has("intensity");
		bool has_energy_to = to_state.has("energy") || to_state.has("intensity");

		if (has_energy_from && has_energy_to) {
			float energy_from = from_state.has("energy") ? (float)from_state["energy"] : (float)from_state["intensity"];
			float energy_to = to_state.has("energy") ? (float)to_state["energy"] : (float)to_state["intensity"];
			float delta = Math::absf(energy_to - energy_from);

			if (delta > coherence_max_energy_delta) {
				float energy_score = 1.0f - ((delta - coherence_max_energy_delta) / (1.0f - coherence_max_energy_delta));
				energy_score = CLAMP(energy_score, 0.0f, 1.0f);
				pair_score = MIN(pair_score, energy_score);

				// Large energy jumps are acceptable with fade-through-silence or stinger
				if (trans_type == TRANSITION_CROSSFADE || trans_type == TRANSITION_CUT) {
					Dictionary diag;
					diag["level"] = (int)(delta > 0.8f ? COHERENCE_ERROR : COHERENCE_WARNING);
					diag["from"] = String(from_name);
					diag["to"] = String(to_name);
					diag["message"] = vformat(
							"Energy jump %.2f (%.2f → %.2f) with %s transition. Consider FADE_THROUGH_SILENCE or STINGER for large energy changes.",
							delta, energy_from, energy_to,
							trans_type == TRANSITION_CROSSFADE ? "CROSSFADE" : "CUT");
					diag["score"] = energy_score;
					diagnostics.push_back(diag);
				}
			}
		}

		// --- Check 2: Tempo Ratio ---
		bool has_bpm_from = from_state.has("bpm");
		bool has_bpm_to = to_state.has("bpm");

		if (has_bpm_from && has_bpm_to) {
			float bpm_from = (float)from_state["bpm"];
			float bpm_to = (float)to_state["bpm"];

			if (bpm_from > 0.0f && bpm_to > 0.0f) {
				float ratio = bpm_to / bpm_from;
				if (ratio < 1.0f) {
					ratio = 1.0f / ratio; // Normalize so ratio >= 1.0
				}

				if (ratio > coherence_max_tempo_ratio) {
					float tempo_score = 1.0f - ((ratio - coherence_max_tempo_ratio) / coherence_max_tempo_ratio);
					tempo_score = CLAMP(tempo_score, 0.0f, 1.0f);
					pair_score = MIN(pair_score, tempo_score);

					// Check if transition duration is long enough for time-stretch
					bool has_time_stretch_room = duration >= 2.0f;

					Dictionary diag;
					diag["level"] = (int)(ratio > 1.5f ? COHERENCE_ERROR : COHERENCE_WARNING);
					diag["from"] = String(from_name);
					diag["to"] = String(to_name);
					diag["message"] = vformat(
							"Tempo change ratio %.2fx (%.0f → %.0f BPM).%s",
							ratio, bpm_from, bpm_to,
							has_time_stretch_room ? "" : " Transition duration (%.1fs) may be too short for smooth tempo ramp. Consider duration >= 2.0s or add a bridge state.");
					diag["score"] = tempo_score;
					diagnostics.push_back(diag);
				}
			}
		}

		// --- Check 3: Key Distance ---
		bool has_key_from = from_state.has("key");
		bool has_key_to = to_state.has("key");

		if (has_key_from && has_key_to) {
			String key_from = String(from_state["key"]);
			String key_to = String(to_state["key"]);

			int semi_from = _key_to_semitone(key_from);
			int semi_to = _key_to_semitone(key_to);

			if (semi_from >= 0 && semi_to >= 0 && semi_from != semi_to) {
				// Allow relative major/minor with no penalty (e.g. C and Am)
				if (!_is_relative_key(key_from, key_to)) {
					int distance = _key_distance_fifths(semi_from, semi_to);

					if (distance > coherence_max_key_distance) {
						float key_score = 1.0f - ((float)(distance - coherence_max_key_distance) / (float)(6 - coherence_max_key_distance));
						key_score = CLAMP(key_score, 0.0f, 1.0f);
						pair_score = MIN(pair_score, key_score);

						// Stingers can mask key changes; crossfades cannot
						bool has_stinger = trans_type == TRANSITION_STINGER;

						Dictionary diag;
						diag["level"] = (int)(distance >= 5 ? COHERENCE_ERROR : COHERENCE_WARNING);
						diag["from"] = String(from_name);
						diag["to"] = String(to_name);
						diag["message"] = vformat(
								"Key distance %d on circle of fifths (%s → %s).%s",
								distance, key_from, key_to,
								has_stinger ? "" : " Consider using a STINGER transition to mask the key change, or add a modulating bridge state.");
						diag["score"] = key_score;
						diagnostics.push_back(diag);
					}
				}
			}
		}

		// --- Check 4: Spectral Centroid Distance ---
		bool has_centroid_from = from_state.has("spectral_centroid_hz");
		bool has_centroid_to = to_state.has("spectral_centroid_hz");

		if (has_centroid_from && has_centroid_to) {
			float centroid_from = (float)from_state["spectral_centroid_hz"];
			float centroid_to = (float)to_state["spectral_centroid_hz"];

			if (centroid_from > 0.0f && centroid_to > 0.0f) {
				float ratio = centroid_to / centroid_from;
				if (ratio < 1.0f) {
					ratio = 1.0f / ratio;
				}

				if (ratio > coherence_max_centroid_ratio) {
					float centroid_score = 1.0f - ((ratio - coherence_max_centroid_ratio) / coherence_max_centroid_ratio);
					centroid_score = CLAMP(centroid_score, 0.0f, 1.0f);
					pair_score = MIN(pair_score, centroid_score);

					Dictionary diag;
					diag["level"] = (int)COHERENCE_WARNING;
					diag["from"] = String(from_name);
					diag["to"] = String(to_name);
					diag["message"] = vformat(
							"Spectral brightness jump %.1fx (%.0f Hz → %.0f Hz). Consider a longer crossfade duration or intermediate state.",
							ratio, centroid_from, centroid_to);
					diag["score"] = centroid_score;
					diagnostics.push_back(diag);
				}
			}
		}
	}

	return diagnostics;
}

bool MusicStateGraph::is_musically_coherent() const {
	TypedArray<Dictionary> diagnostics = validate_musical_coherence();
	for (int i = 0; i < diagnostics.size(); i++) {
		Dictionary d = diagnostics[i];
		if ((int)d.get("level", 0) == COHERENCE_ERROR) {
			return false;
		}
	}
	return true;
}

float MusicStateGraph::get_transition_coherence_score(const StringName &p_from, const StringName &p_to) const {
	if (!states.has(p_from) || !states.has(p_to)) {
		return 1.0f;
	}

	// Check for coherence_override on the transition
	Dictionary t = find_transition(p_from, p_to);
	if (!t.is_empty() && (bool)t.get("coherence_override", false)) {
		return 1.0f;
	}

	Dictionary from_state = states[p_from];
	Dictionary to_state = states[p_to];

	float score = 1.0f;

	// Energy
	if ((from_state.has("energy") || from_state.has("intensity")) &&
			(to_state.has("energy") || to_state.has("intensity"))) {
		float e_from = from_state.has("energy") ? (float)from_state["energy"] : (float)from_state["intensity"];
		float e_to = to_state.has("energy") ? (float)to_state["energy"] : (float)to_state["intensity"];
		float delta = Math::absf(e_to - e_from);
		if (delta > coherence_max_energy_delta) {
			float s = 1.0f - ((delta - coherence_max_energy_delta) / (1.0f - coherence_max_energy_delta));
			score = MIN(score, CLAMP(s, 0.0f, 1.0f));
		}
	}

	// Tempo
	if (from_state.has("bpm") && to_state.has("bpm")) {
		float bpm_from = (float)from_state["bpm"];
		float bpm_to = (float)to_state["bpm"];
		if (bpm_from > 0.0f && bpm_to > 0.0f) {
			float ratio = bpm_to / bpm_from;
			if (ratio < 1.0f) ratio = 1.0f / ratio;
			if (ratio > coherence_max_tempo_ratio) {
				float s = 1.0f - ((ratio - coherence_max_tempo_ratio) / coherence_max_tempo_ratio);
				score = MIN(score, CLAMP(s, 0.0f, 1.0f));
			}
		}
	}

	// Key
	if (from_state.has("key") && to_state.has("key")) {
		String key_from = String(from_state["key"]);
		String key_to = String(to_state["key"]);
		int semi_from = _key_to_semitone(key_from);
		int semi_to = _key_to_semitone(key_to);
		if (semi_from >= 0 && semi_to >= 0 && semi_from != semi_to) {
			if (!_is_relative_key(key_from, key_to)) {
				int distance = _key_distance_fifths(semi_from, semi_to);
				if (distance > coherence_max_key_distance) {
					float s = 1.0f - ((float)(distance - coherence_max_key_distance) / (float)(6 - coherence_max_key_distance));
					score = MIN(score, CLAMP(s, 0.0f, 1.0f));
				}
			}
		}
	}

	// Spectral centroid
	if (from_state.has("spectral_centroid_hz") && to_state.has("spectral_centroid_hz")) {
		float c_from = (float)from_state["spectral_centroid_hz"];
		float c_to = (float)to_state["spectral_centroid_hz"];
		if (c_from > 0.0f && c_to > 0.0f) {
			float ratio = c_to / c_from;
			if (ratio < 1.0f) ratio = 1.0f / ratio;
			if (ratio > coherence_max_centroid_ratio) {
				float s = 1.0f - ((ratio - coherence_max_centroid_ratio) / coherence_max_centroid_ratio);
				score = MIN(score, CLAMP(s, 0.0f, 1.0f));
			}
		}
	}

	return score;
}
