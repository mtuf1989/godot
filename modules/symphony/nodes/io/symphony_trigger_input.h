#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

#include <atomic>

// Exposes a named trigger input to the game thread (GDScript API).
// Game thread writes pending triggers; audio thread drains them at micro-block start.
// Outputs a TRIGGER pin.
class SymphonyTriggerInput : public SymphonyOperator {
private:
	TriggerBuffer *output = nullptr;

	// Lock-free pending trigger from game thread.
	// Simple approach: one pending trigger per micro-block (game tick >> audio block rate).
	std::atomic<float> pending_value{ 0.0f };
	std::atomic<bool> pending_flag{ false };

public:
	SymphonyTriggerInput() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (TriggerBuffer *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		// Output buffer is already cleared by CompiledGraph::execute().
		if (pending_flag.load(std::memory_order_acquire)) {
			float val = pending_value.load(std::memory_order_relaxed);
			output->push(0, val); // Fire at sample 0 of this micro-block.
			pending_flag.store(false, std::memory_order_release);
		}
	}

	// Called from game thread via trigger() routing.
	void fire(float p_value = 1.0f) {
		pending_value.store(p_value, std::memory_order_relaxed);
		pending_flag.store(true, std::memory_order_release);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "TriggerInput";
		desc.outputs.push_back({ "output", SymphonyPinType::TRIGGER, false });
		desc.state_size = sizeof(SymphonyTriggerInput);
		desc.state_align = alignof(SymphonyTriggerInput);
		desc.create_fn = &SymphonyTriggerInput::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyTriggerInput), alignof(SymphonyTriggerInput));
		return new (mem) SymphonyTriggerInput();
	}
};
