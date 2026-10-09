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

	bool pending_on = false;
	bool pending_off = false;
	float pending_on_value = 1.0f;
	int32_t pending_on_offset = 0;
	int32_t pending_off_offset = 0;

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
		if (pending_on) {
			if (note_on_out) {
				note_on_out->push(CLAMP(pending_on_offset, 0, MAX(p_num_frames - 1, 0)), pending_on_value);
			}
			if (adsr_trigger_out) {
				adsr_trigger_out->push(CLAMP(pending_on_offset, 0, MAX(p_num_frames - 1, 0)), pending_on_value);
			}
			pending_on = false;
		}
		if (pending_off) {
			if (note_off_out) {
				note_off_out->push(CLAMP(pending_off_offset, 0, MAX(p_num_frames - 1, 0)), 1.0f);
			}
			if (adsr_trigger_out) {
				adsr_trigger_out->push(CLAMP(pending_off_offset, 0, MAX(p_num_frames - 1, 0)), 0.0f);
			}
			pending_off = false;
		}
	}

	// Audio-thread: apply a NoteOn at a sample offset within the current micro-block.
	void note_on(float p_frequency_hz, float p_velocity, int32_t p_offset = 0) {
		frequency = MAX(p_frequency_hz, 0.0f);
		velocity = CLAMP(p_velocity, 0.0f, 1.0f);
		pitch_ratio = CLAMP(frequency / root_hz, 0.125f, 8.0f);
		gate = 1.0f;
		pending_on = true;
		pending_on_value = velocity > 0.0f ? velocity : 1.0f;
		pending_on_offset = p_offset;
		pending_off = false;
	}

	// Audio-thread: apply a NoteOff at a sample offset within the current micro-block.
	void note_off(int32_t p_offset = 0) {
		gate = 0.0f;
		pending_off = true;
		pending_off_offset = p_offset;
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
		pending_on = false;
		pending_off = false;
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
