#ifndef MUSIC_STATE_GRAPH_H
#define MUSIC_STATE_GRAPH_H

#include "core/io/resource.h"
#include "servers/audio/audio_stream.h"

class MusicStateGraph : public Resource {
	GDCLASS(MusicStateGraph, Resource);

public:
	enum TransitionType { TRANSITION_CROSSFADE = 0, TRANSITION_FADE_THROUGH_SILENCE, TRANSITION_CUT, TRANSITION_STINGER };
	enum Quantization { QUANTIZE_IMMEDIATE = 0, QUANTIZE_NEXT_BEAT, QUANTIZE_NEXT_BAR };

	// Musical coherence warning severity levels.
	enum CoherenceLevel { COHERENCE_OK = 0, COHERENCE_WARNING, COHERENCE_ERROR };

	// A single coherence diagnostic entry.
	struct CoherenceDiagnostic {
		CoherenceLevel level = COHERENCE_OK;
		String from_state;
		String to_state;
		String message;
		float score = 1.0f; // 0.0 = incoherent, 1.0 = perfectly coherent
	};

private:
	// States: Dictionary<StringName, Dictionary> where each state dict has:
	//   "stream": AudioStream, "bpm": float, "beats_per_bar": int, "loop": bool,
	//   "layers": Array[Dictionary] (each: {"stream": AudioStream, "name": StringName, "default_active": bool, "fade_time": float})
	//
	//   --- Musical metadata (optional, used by validate_musical_coherence) ---
	//   "key": String (e.g. "Am", "C", "F#m", "Bb") — musical key of this state
	//   "energy": float (0.0-1.0) — perceived energy level
	//   "spectral_centroid_hz": float — average brightness (Hz)
	//   "intensity": float (0.0-1.0) — synonym for energy, either field accepted
	//   Note: "bpm" already exists in the state schema and is used for tempo coherence.
	Dictionary states;

	// Transitions: Array[Dictionary] where each dict has:
	//   "from": StringName (or "*"), "to": StringName (or "*"),
	//   "type": int (TransitionType), "duration": float,
	//   "quantization": int (Quantization), "stinger": AudioStream,
	//   "coherence_override": bool (optional, default false — suppresses coherence warnings for this transition),
	//   "auto_curve_selection": bool (optional, default true — enables TransitionAnalyzer auto curve picking)
	TypedArray<Dictionary> transitions;

	StringName initial_state;

	// --- Coherence thresholds (configurable per graph) ---
	float coherence_max_energy_delta = 0.5f;      // Energy jump > this without fade-through-silence triggers warning
	float coherence_max_tempo_ratio = 1.2f;        // Tempo ratio > this (or < 1/this) triggers warning
	float coherence_max_centroid_ratio = 2.0f;     // Spectral centroid ratio > this triggers warning
	int coherence_max_key_distance = 3;            // Semitone distance on circle of fifths > this triggers warning

	// Internal helpers
	static int _key_to_semitone(const String &p_key);
	static int _key_distance_fifths(int p_semi_a, int p_semi_b);
	static bool _is_relative_key(const String &p_key_a, const String &p_key_b);

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_states(const Dictionary &p_states) { states = p_states; }
	Dictionary get_states() const { return states; }

	void set_transitions(const TypedArray<Dictionary> &p_transitions) { transitions = p_transitions; }
	TypedArray<Dictionary> get_transitions() const { return transitions; }

	void set_initial_state(const StringName &p_state) { initial_state = p_state; }
	StringName get_initial_state() const { return initial_state; }

	// Coherence threshold setters/getters
	void set_coherence_max_energy_delta(float p_delta) { coherence_max_energy_delta = p_delta; }
	float get_coherence_max_energy_delta() const { return coherence_max_energy_delta; }
	void set_coherence_max_tempo_ratio(float p_ratio) { coherence_max_tempo_ratio = p_ratio; }
	float get_coherence_max_tempo_ratio() const { return coherence_max_tempo_ratio; }
	void set_coherence_max_centroid_ratio(float p_ratio) { coherence_max_centroid_ratio = p_ratio; }
	float get_coherence_max_centroid_ratio() const { return coherence_max_centroid_ratio; }
	void set_coherence_max_key_distance(int p_dist) { coherence_max_key_distance = p_dist; }
	int get_coherence_max_key_distance() const { return coherence_max_key_distance; }

	// Helpers for GDScript
	Dictionary get_state(const StringName &p_name) const;
	Dictionary find_transition(const StringName &p_from, const StringName &p_to) const;
	PackedStringArray get_state_names() const;
	bool validate() const;

	// Musical coherence validation (Item 2).
	// Checks all transitions for musical coherence based on optional metadata fields.
	// Returns an array of diagnostic dictionaries:
	//   {level: int, from: String, to: String, message: String, score: float}
	// Only evaluates transitions where both source and target states have metadata.
	// Transitions with "coherence_override": true are skipped.
	TypedArray<Dictionary> validate_musical_coherence() const;

	// Convenience: returns true if no errors (warnings are acceptable).
	bool is_musically_coherent() const;

	// Get the coherence score for a specific transition pair (0.0-1.0).
	// Returns 1.0 if either state lacks metadata or if the pair has coherence_override.
	float get_transition_coherence_score(const StringName &p_from, const StringName &p_to) const;
};

VARIANT_ENUM_CAST(MusicStateGraph::TransitionType);
VARIANT_ENUM_CAST(MusicStateGraph::Quantization);
VARIANT_ENUM_CAST(MusicStateGraph::CoherenceLevel);

#endif // MUSIC_STATE_GRAPH_H
