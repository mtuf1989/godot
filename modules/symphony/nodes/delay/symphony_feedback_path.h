#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_pin_types.h"

#include <cstring>

// FeedbackPath: marks a feedback edge in the DSP graph.
//
// P9 zero-extra: this is a pass-through (copy input → output). The one-block
// delay comes from the persistent pin buffer: the compiler schedules FeedbackPath
// last among its non-feedback deps, so readers wired via is_feedback edges run
// earlier in the same block and still see the previous block's output.
// Bare feedback edges and FeedbackPath loops are exactly 1 micro-block
// (64 native / 32 web). Short loops belong inside one operator.
class SymphonyFeedbackPath : public SymphonyOperator {
private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

public:
	SymphonyFeedbackPath() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);
		if (!audio_out) {
			return;
		}
		if (audio_in) {
			if (audio_in != audio_out) {
				memcpy(audio_out, audio_in, sizeof(float) * (size_t)p_num_frames);
			}
		} else {
			memset(audio_out, 0, sizeof(float) * (size_t)p_num_frames);
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		(void)p_buffer;
		(void)p_max_size;
		return 0;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		(void)p_buffer;
		(void)p_size;
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "FeedbackPath";
		desc.category = "Delay";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false }); // Not required at compile - wired by feedback detection
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.state_size = sizeof(SymphonyFeedbackPath);
		desc.state_align = alignof(SymphonyFeedbackPath);
		desc.extra_arena_bytes = 0;
		desc.resettable = true;
		desc.create_fn = &SymphonyFeedbackPath::create;
		desc.silence_behavior = SilenceBehavior::ALWAYS_PROCESS;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_params;
		(void)p_mix_rate;
		void *mem = p_arena.alloc(sizeof(SymphonyFeedbackPath), alignof(SymphonyFeedbackPath));
		return new (mem) SymphonyFeedbackPath();
	}
};
