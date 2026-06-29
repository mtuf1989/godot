#ifndef MUSIC_STATE_GRAPH_H
#define MUSIC_STATE_GRAPH_H

#include "core/io/resource.h"
#include "servers/audio/audio_stream.h"

class MusicStateGraph : public Resource {
	GDCLASS(MusicStateGraph, Resource);

public:
	enum TransitionType { TRANSITION_CROSSFADE = 0, TRANSITION_FADE_THROUGH_SILENCE, TRANSITION_CUT, TRANSITION_STINGER };
	enum Quantization { QUANTIZE_IMMEDIATE = 0, QUANTIZE_NEXT_BEAT, QUANTIZE_NEXT_BAR };

private:
	// States: Dictionary<StringName, Dictionary> where each state dict has:
	//   "stream": AudioStream, "bpm": float, "beats_per_bar": int, "loop": bool,
	//   "layers": Array[Dictionary] (each: {"stream": AudioStream, "name": StringName, "default_active": bool, "fade_time": float})
	Dictionary states;

	// Transitions: Array[Dictionary] where each dict has:
	//   "from": StringName (or "*"), "to": StringName (or "*"),
	//   "type": int (TransitionType), "duration": float,
	//   "quantization": int (Quantization), "stinger": AudioStream
	TypedArray<Dictionary> transitions;

	StringName initial_state;

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

	// Helpers for GDScript
	Dictionary get_state(const StringName &p_name) const;
	Dictionary find_transition(const StringName &p_from, const StringName &p_to) const;
	PackedStringArray get_state_names() const;
	bool validate() const;
};

VARIANT_ENUM_CAST(MusicStateGraph::TransitionType);
VARIANT_ENUM_CAST(MusicStateGraph::Quantization);

#endif // MUSIC_STATE_GRAPH_H
