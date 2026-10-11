/**************************************************************************/
/*  symphony_note_pool.h                                                  */
/**************************************************************************/
/* Precompiled instrument voice pools for music N2.                       */
/**************************************************************************/

#pragma once

#include "../core/symphony_graph_compiler.h"
#include "../core/symphony_graph_description.h"
#include "../core/symphony_operator_registry.h"
#include "../core/symphony_pin_types.h"
#include "../core/symphony_seed.h"
#include "../nodes/io/symphony_graph_output.h"
#include "../nodes/io/symphony_music_note_input.h"
#include "servers/audio/audio_frame.h"

#include "core/math/math_funcs.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"

#include <cstdint>

// MIDI note number → Hz (A4=440).
[[nodiscard]] inline float symphony_midi_to_hz(int32_t p_midi) {
	return 440.0f * Math::pow(2.0f, ((float)p_midi - 69.0f) / 12.0f);
}

// Reject graphs that contain non-resettable operators (allow-list matches P10 test).
[[nodiscard]] inline bool symphony_graph_is_note_pool_safe(const GraphDescription &p_desc, Vector<String> *r_errors = nullptr) {
	OperatorRegistry *reg = OperatorRegistry::get_singleton();
	if (!reg) {
		if (r_errors) {
			r_errors->push_back("OperatorRegistry missing.");
		}
		return false;
	}
	// Only I/O sinks may be non-resettable; everything else must opt into reset_for_note.
	static const char *k_io_allow[] = { "GraphOutput" };
	bool ok = true;
	bool has_note_input = false;
	for (int i = 0; i < p_desc.nodes.size(); i++) {
		const NodeDesc &nd = p_desc.nodes[i];
		if (nd.type_name == StringName("MusicNoteInput")) {
			has_note_input = true;
		}
		const OperatorDescriptor *desc = reg->find(nd.type_name);
		if (!desc) {
			ok = false;
			if (r_errors) {
				r_errors->push_back(vformat("Unknown operator '%s'.", String(nd.type_name)));
			}
			continue;
		}
		if (desc->resettable) {
			continue;
		}
		bool allowed = false;
		for (const char *name : k_io_allow) {
			if (nd.type_name == StringName(name)) {
				allowed = true;
				break;
			}
		}
		if (!allowed) {
			ok = false;
			if (r_errors) {
				r_errors->push_back(vformat("Operator '%s' is not resettable for note pools.", String(nd.type_name)));
			}
		}
	}
	if (!has_note_input) {
		ok = false;
		if (r_errors) {
			r_errors->push_back("Instrument graph requires exactly one MusicNoteInput.");
		}
	}
	return ok;
}

// One precompiled voice slot in a track's note pool.
struct SymphonyNoteVoice {
	CompiledGraph *graph = nullptr;
	SymphonyMusicNoteInput *note_input = nullptr;
	SymphonyGraphOutput *graph_output = nullptr;
	bool active = false;
	bool releasing = false;
	bool protected_track = false;
	int32_t note_id = -1;
	int32_t generation = 0;
	int32_t midi = 0;
	float velocity = 0.0f;
	uint64_t voice_seed = 0;
	uint64_t start_frame = 0;
	uint64_t release_deadline_frame = UINT64_MAX;

	void reset_idle() {
		active = false;
		releasing = false;
		note_id = -1;
		generation = 0;
		midi = 0;
		velocity = 0.0f;
		start_frame = 0;
		release_deadline_frame = UINT64_MAX;
	}
};

// Fixed-capacity pool of note voices for one instrument graph / track.
class SymphonyNotePool {
public:
	static constexpr int32_t MAX_VOICES = 32;

	struct ForcedOff {
		int32_t generation = 0;
		int32_t note_id = 0;
	};

	SymphonyNoteVoice voices[MAX_VOICES];
	int32_t voice_count = 0;
	GraphDescription instrument;
	float mix_rate = 48000.0f;
	int32_t track_id = 0;
	bool protected_track = false;
	int32_t max_voices = 8;
	int32_t dropped_optional = 0;
	float max_hold_seconds = 60.0f;
	String last_error;
	AudioFrame render_tmp[64];
	ForcedOff forced_offs[MAX_VOICES];
	int32_t forced_count = 0;

	~SymphonyNotePool() { clear(); }

	void clear() {
		for (int i = 0; i < voice_count; i++) {
			if (voices[i].graph) {
				memdelete(voices[i].graph);
				voices[i].graph = nullptr;
			}
		}
		voice_count = 0;
		forced_count = 0;
	}

	int drain_forced(ForcedOff *p_out, int p_max) {
		const int n = MIN(forced_count, p_max);
		for (int i = 0; i < n; i++) {
			p_out[i] = forced_offs[i];
		}
		forced_count = 0;
		return n;
	}

	bool prepare(const GraphDescription &p_instrument, float p_mix_rate, int32_t p_voice_count, uint64_t p_pool_seed) {
		clear();
		last_error.clear();
		Vector<String> errors;
		if (!symphony_graph_is_note_pool_safe(p_instrument, &errors)) {
			last_error = errors.size() ? errors[0] : String("unsafe graph");
			return false;
		}
		int note_inputs = 0;
		for (int i = 0; i < p_instrument.nodes.size(); i++) {
			if (p_instrument.nodes[i].type_name == StringName("MusicNoteInput")) {
				note_inputs++;
			}
		}
		if (note_inputs != 1) {
			last_error = "Instrument graph requires exactly one MusicNoteInput.";
			return false;
		}

		GraphDescription desc = p_instrument;
		desc.smooth_parameters = false; // discrete pitch/velocity — no auto-smoother
		mix_rate = p_mix_rate;
		max_voices = CLAMP(p_voice_count, 1, MAX_VOICES);
		instrument = desc;

		voice_count = max_voices;
		for (int v = 0; v < max_voices; v++) {
			const uint64_t seed = splitmix64(p_pool_seed + (uint64_t)v * 0x9E3779B97F4A7C15ull);
			GraphCompiler::CompileOptions opts;
			opts.capture_pristine = true;
			GraphCompiler::CompileResult cr = GraphCompiler::compile(desc, mix_rate, String(), seed, nullptr, nullptr, opts);
			if (!cr.success()) {
				last_error = cr.errors.size() ? String(cr.errors[0]) : String("compile failed");
				clear();
				return false;
			}
			SymphonyNoteVoice &slot = voices[v];
			slot.graph = cr.graph;
			slot.voice_seed = seed;
			slot.protected_track = protected_track;
			slot.note_input = nullptr;
			slot.graph_output = nullptr;
			for (int32_t oi = 0; oi < slot.graph->operator_count; oi++) {
				if (slot.graph->operator_types[oi] == StringName("MusicNoteInput")) {
					slot.note_input = dynamic_cast<SymphonyMusicNoteInput *>(slot.graph->operators[oi]);
				}
				if (!slot.graph_output) {
					slot.graph_output = dynamic_cast<SymphonyGraphOutput *>(slot.graph->operators[oi]);
				}
			}
			if (!slot.note_input || !slot.graph_output) {
				last_error = "Compiled graph missing MusicNoteInput or GraphOutput.";
				clear();
				return false;
			}
			slot.reset_idle();
		}
		return true;
	}

	// Admit NoteOn. Returns voice index or -1 if dropped.
	int32_t note_on(int32_t p_note_id, int32_t p_midi, float p_velocity, uint64_t p_frame, int32_t p_offset, int32_t p_generation = 0) {
		int32_t free_idx = -1;
		for (int i = 0; i < voice_count; i++) {
			if (!voices[i].active) {
				free_idx = i;
				break;
			}
		}
		if (free_idx < 0) {
			// Steal the oldest releasing voice, then the oldest active voice. Never steal a protected voice.
			int32_t victim = -1;
			uint64_t oldest = UINT64_MAX;
			bool found_releasing = false;
			for (int i = 0; i < voice_count; i++) {
				if (voices[i].protected_track) {
					continue;
				}
				if (voices[i].releasing) {
					if (!found_releasing || voices[i].start_frame < oldest) {
						oldest = voices[i].start_frame;
						victim = i;
						found_releasing = true;
					}
				} else if (!found_releasing && voices[i].start_frame < oldest) {
					oldest = voices[i].start_frame;
					victim = i;
				}
			}
			if (victim < 0) {
				if (!protected_track) {
					dropped_optional++;
				}
				return -1;
			}
			free_idx = victim;
		}

		SymphonyNoteVoice &slot = voices[free_idx];
		const uint64_t seed = splitmix64(slot.voice_seed ^ ((uint64_t)p_note_id << 32) ^ (uint64_t)(uint32_t)p_midi ^ (uint64_t)(uint32_t)p_generation);
		for (int32_t oi = 0; oi < slot.graph->operator_count; oi++) {
			slot.graph->operators[oi]->reset_for_note(seed);
		}
		slot.voice_seed = seed;
		slot.active = true;
		slot.releasing = false;
		slot.note_id = p_note_id;
		slot.generation = p_generation;
		slot.midi = p_midi;
		slot.velocity = CLAMP(p_velocity, 0.0f, 1.0f);
		slot.start_frame = p_frame;
		slot.release_deadline_frame = UINT64_MAX;
		slot.note_input->note_on(symphony_midi_to_hz(p_midi), slot.velocity, p_offset);
		return free_idx;
	}

	bool note_off(int32_t p_note_id, uint64_t p_frame, int32_t p_offset, uint64_t p_release_frames = 48000, int32_t p_generation = 0) {
		bool any = false;
		for (int i = 0; i < voice_count; i++) {
			SymphonyNoteVoice &slot = voices[i];
			if (!slot.active || slot.note_id != p_note_id || slot.generation != p_generation || slot.releasing) {
				continue;
			}
			slot.note_input->note_off(p_offset);
			slot.releasing = true;
			slot.release_deadline_frame = p_frame + p_release_frames;
			any = true;
		}
		return any;
	}

	// Mix active voices into p_out for p_frames; reclaim silent/expired releases.
	// No heap traffic: scratch is the member render_tmp[64].
	void render(AudioFrame *p_out, int p_frames, uint64_t p_block_start_frame) {
		for (int i = 0; i < p_frames; i++) {
			p_out[i] = AudioFrame(0.0f, 0.0f);
		}
		const uint64_t block_end = p_block_start_frame + (uint64_t)MAX(p_frames, 0);
		const uint64_t hold_frames = (uint64_t)MAX(0.0f, max_hold_seconds * mix_rate);
		for (int v = 0; v < voice_count; v++) {
			SymphonyNoteVoice &slot = voices[v];
			if (!slot.active) {
				continue;
			}
			float peak = 0.0f;
			for (int off = 0; off < p_frames; off += 64) {
				const int block = MIN(64, p_frames - off);
				for (int i = 0; i < block; i++) {
					render_tmp[i] = AudioFrame(0.0f, 0.0f);
				}
				slot.graph_output->set_output(render_tmp, 0);
				slot.graph->execute(block);
				for (int i = 0; i < block; i++) {
					p_out[off + i] += render_tmp[i];
					peak = MAX(peak, MAX(Math::abs(render_tmp[i].left), Math::abs(render_tmp[i].right)));
				}
			}
			if (!slot.releasing && hold_frames > 0 && block_end > slot.start_frame + hold_frames) {
				slot.note_input->note_off(0);
				slot.releasing = true;
				slot.release_deadline_frame = block_end + (uint64_t)MAX(1.0f, 0.05f * mix_rate);
				if (forced_count < MAX_VOICES) {
					forced_offs[forced_count].generation = slot.generation;
					forced_offs[forced_count].note_id = slot.note_id;
					forced_count++;
				}
			}
			if (slot.releasing) {
				bool silent = peak < 1e-5f;
				for (int32_t oi = 0; oi < slot.graph->operator_count && silent; oi++) {
					if (slot.graph->operators[oi]->activity != 0) {
						silent = false;
					}
				}
				if (silent || block_end >= slot.release_deadline_frame) {
					slot.reset_idle();
				}
			}
		}
	}

	int32_t active_count() const {
		int32_t n = 0;
		for (int i = 0; i < voice_count; i++) {
			if (voices[i].active) {
				n++;
			}
		}
		return n;
	}
};
