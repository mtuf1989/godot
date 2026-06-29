#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_pin_types.h"

// FeedbackPath: enables cycles in the DSP graph.
// Stores one micro-block of audio from the previous execution cycle.
// On execute(), outputs the stored buffer, then copies input into storage.
// This introduces exactly 1 micro-block of latency in the feedback loop.
class SymphonyFeedbackPath : public SymphonyOperator {
private:
	const float *__restrict__ audio_in = nullptr;
	float *__restrict__ audio_out = nullptr;

	float *prev_block = nullptr; // Arena-allocated buffer for previous block

public:
	SymphonyFeedbackPath() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Output the previous block's stored data
		for (int32_t i = 0; i < p_num_frames; i++) {
			audio_out[i] = prev_block[i];
		}

		// Store current input for next cycle
		if (audio_in) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				prev_block[i] = audio_in[i];
			}
		} else {
			for (int32_t i = 0; i < p_num_frames; i++) {
				prev_block[i] = 0.0f;
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		size_t needed = sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		memcpy(p_buffer, prev_block, needed);
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE;
		if (p_size < needed) return;
		memcpy(prev_block, p_buffer, needed);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "FeedbackPath";
		desc.category = "Delay";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false }); // Not required at compile - wired by feedback detection
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyFeedbackPath);
		desc.state_align = alignof(SymphonyFeedbackPath);
		desc.create_fn = &SymphonyFeedbackPath::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyFeedbackPath), alignof(SymphonyFeedbackPath));
		SymphonyFeedbackPath *fb = new (mem) SymphonyFeedbackPath();
		fb->prev_block = (float *)p_arena.alloc(sizeof(float) * SYMPHONY_MICRO_BLOCK_SIZE, 32);
		return fb;
	}
};
