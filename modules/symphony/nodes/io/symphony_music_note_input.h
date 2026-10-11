#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"
#include "core/math/math_funcs.h"

// MusicNoteInput: single control hub for a note-voice instrument graph (music N2).
// The note pool writes pitch/velocity/gate at the event boundary (no param smoothing).
// Outputs:
//   frequency, velocity, pitch_ratio (FLOAT) — pitch_ratio = frequency / root_hz
//   note_on, note_off (TRIGGER) — WavePlayer / strike paths use note_on only
//   gate (FLOAT 0/1)
//   adsr_trigger (TRIGGER) — +1 on note-on, 0 on note-off (ADSR convenience)
class SymphonyMusicNoteInput : public SymphonyOperator {
private:
	float *frequency_out = nullptr;
	float *velocity_out = nullptr;
	float *pitch_ratio_out = nullptr;
	TriggerBuffer *note_on_out = nullptr;
	TriggerBuffer *note_off_out = nullptr;
	float *gate_out = nullptr;
	TriggerBuffer *adsr_trigger_out = nullptr;

	float root_hz = 440.0f;
	float frequency = 440.0f;
	float velocity = 0.0f;
	float pitch_ratio = 1.0f;
	float gate = 0.0f;

	struct PendingEvent {
		int32_t offset = 0;
		bool is_on = false;
		float value = 1.0f;
	};
	static constexpr int MAX_PENDING = 4;
	PendingEvent pending[MAX_PENDING];
	int32_t pending_count = 0;

	void _queue(int32_t p_offset, bool p_on, float p_value) {
		if (pending_count >= MAX_PENDING) {
			pending_count = MAX_PENDING - 1;
		}
		PendingEvent &ev = pending[pending_count++];
		ev.offset = p_offset;
		ev.is_on = p_on;
		ev.value = p_value;
	}

public:
	SymphonyMusicNoteInput(float p_root_hz) {
		root_hz = MAX(p_root_hz, 1.0f);
		frequency = root_hz;
		pitch_ratio = 1.0f;
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		(void)p_input_ptrs;
		frequency_out = (float *)p_output_ptrs[0];
		velocity_out = (float *)p_output_ptrs[1];
		pitch_ratio_out = (float *)p_output_ptrs[2];
		note_on_out = (TriggerBuffer *)p_output_ptrs[3];
		note_off_out = (TriggerBuffer *)p_output_ptrs[4];
		gate_out = (float *)p_output_ptrs[5];
		adsr_trigger_out = (TriggerBuffer *)p_output_ptrs[6];
	}

	virtual void execute(int32_t p_num_frames) override {
		if (frequency_out) {
			frequency_out[0] = frequency;
		}
		if (velocity_out) {
			velocity_out[0] = velocity;
		}
		if (pitch_ratio_out) {
			pitch_ratio_out[0] = pitch_ratio;
		}
		if (gate_out) {
			gate_out[0] = gate;
		}
		for (int32_t i = 1; i < pending_count; i++) {
			PendingEvent key = pending[i];
			int32_t j = i - 1;
			while (j >= 0 && pending[j].offset > key.offset) {
				pending[j + 1] = pending[j];
				j--;
			}
			pending[j + 1] = key;
		}
		for (int32_t i = 0; i < pending_count; i++) {
			const int32_t off = CLAMP(pending[i].offset, 0, MAX(p_num_frames - 1, 0));
			if (pending[i].is_on) {
				if (note_on_out) {
					note_on_out->push(off, pending[i].value);
				}
				if (adsr_trigger_out) {
					adsr_trigger_out->push(off, pending[i].value);
				}
			} else {
				if (note_off_out) {
					note_off_out->push(off, 1.0f);
				}
				if (adsr_trigger_out) {
					adsr_trigger_out->push(off, 0.0f);
				}
			}
		}
		pending_count = 0;
	}

	// Audio-thread: apply a NoteOn at a sample offset within the current micro-block.
	void note_on(float p_frequency_hz, float p_velocity, int32_t p_offset = 0) {
		frequency = MAX(p_frequency_hz, 0.0f);
		velocity = CLAMP(p_velocity, 0.0f, 1.0f);
		pitch_ratio = CLAMP(frequency / root_hz, 0.125f, 8.0f);
		gate = 1.0f;
		_queue(p_offset, true, velocity > 0.0f ? velocity : 1.0f);
	}

	// Audio-thread: apply a NoteOff at a sample offset within the current micro-block.
	void note_off(int32_t p_offset = 0) {
		gate = 0.0f;
		_queue(p_offset, false, 1.0f);
	}

	float get_frequency() const { return frequency; }
	float get_velocity() const { return velocity; }
	float get_pitch_ratio() const { return pitch_ratio; }
	float get_gate() const { return gate; }
	float get_root_hz() const { return root_hz; }

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = 4 * sizeof(float);
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		float state[4] = { frequency, velocity, pitch_ratio, gate };
		memcpy(p_buffer, state, needed);
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		if (p_size < 4 * sizeof(float)) {
			return;
		}
		float state[4];
		memcpy(state, p_buffer, 4 * sizeof(float));
		frequency = state[0];
		velocity = state[1];
		pitch_ratio = state[2];
		gate = state[3];
		pending_count = 0;
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "MusicNoteInput";
		desc.category = "I/O";
		desc.outputs.push_back({ "frequency", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "velocity", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "pitch_ratio", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "note_on", SymphonyPinType::TRIGGER, false });
		desc.outputs.push_back({ "note_off", SymphonyPinType::TRIGGER, false });
		desc.outputs.push_back({ "gate", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "adsr_trigger", SymphonyPinType::TRIGGER, false });
		desc.params.push_back({ "root_hz", 440.0f, 1.0f, 20000.0f, 0.1f });
		desc.state_size = sizeof(SymphonyMusicNoteInput);
		desc.state_align = alignof(SymphonyMusicNoteInput);
		desc.export_state_bytes = 16;
		desc.resettable = true;
		desc.create_fn = &SymphonyMusicNoteInput::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		float root = p_params.has("root_hz") ? (float)p_params["root_hz"] : 440.0f;
		void *mem = p_arena.alloc(sizeof(SymphonyMusicNoteInput), alignof(SymphonyMusicNoteInput));
		if (!mem) {
			return nullptr;
		}
		return new (mem) SymphonyMusicNoteInput(root);
	}
};
