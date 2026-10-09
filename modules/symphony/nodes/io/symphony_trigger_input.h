#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"

#include <atomic>
#include <cstdint>

// Exposes a named trigger input to the game thread (GDScript API).
// Fixed 64-entry SPSC queue: game thread produces, audio drains (plan §9).
// target_frame 0 = ASAP; otherwise absolute graph-rate frame (N1 / P7 R).
class SymphonyTriggerInput : public SymphonyOperator {
private:
	TriggerBuffer *output = nullptr;

	static constexpr uint32_t QUEUE_CAPACITY = 64;
	struct QueueEntry {
		float value = 1.0f;
		uint64_t target_frame = 0; // 0 = ASAP
	};
	QueueEntry queue[QUEUE_CAPACITY];
	std::atomic<uint32_t> write_pos{ 0 };
	std::atomic<uint32_t> read_pos{ 0 };

	bool auto_trigger_on_play = true;

	// Audio-thread-only injection slot (auto-trigger on play); consumed in execute().
	bool local_pending = false;
	float local_value = 1.0f;

	// Set by playback before each micro-block execute (absolute graph frame).
	uint64_t block_start_frame = 0;

public:
	SymphonyTriggerInput() {}

	void set_block_start_frame(uint64_t p_frame) { block_start_frame = p_frame; }

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		output = (TriggerBuffer *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		if (!output || p_num_frames <= 0) {
			return;
		}
		// Runs after the graph cleared trigger buffers for this block.
		if (local_pending) {
			if (!output->push(0, local_value)) {
				symphony_note_dropped_trigger();
			}
			local_pending = false;
		}
		uint32_t r = read_pos.load(std::memory_order_relaxed);
		uint32_t w = write_pos.load(std::memory_order_acquire);
		const uint64_t block_end = block_start_frame + (uint64_t)p_num_frames;
		while (r != w) {
			const QueueEntry &e = queue[r % QUEUE_CAPACITY];
			if (e.target_frame != 0 && e.target_frame >= block_end) {
				// Future — leave queued for a later block (FIFO).
				break;
			}
			int32_t offset = 0;
			if (e.target_frame != 0) {
				if (e.target_frame > block_start_frame) {
					offset = (int32_t)(e.target_frame - block_start_frame);
				}
				if (offset >= p_num_frames) {
					offset = p_num_frames - 1;
				}
				if (offset < 0) {
					offset = 0;
				}
			}
			if (!output->push(offset, e.value)) {
				symphony_note_dropped_trigger();
				// Leave remaining entries queued for the next block.
				break;
			}
			r++;
		}
		read_pos.store(r, std::memory_order_release);
	}

	// Called from game thread via trigger() routing. Returns false if queue is full.
	bool fire(float p_value = 1.0f) {
		return fire_at(0, p_value);
	}

	// Absolute graph-rate frame; 0 = ASAP (same as fire).
	bool fire_at(uint64_t p_target_frame, float p_value = 1.0f) {
		uint32_t w = write_pos.load(std::memory_order_relaxed);
		uint32_t r = read_pos.load(std::memory_order_acquire);
		if ((w - r) >= QUEUE_CAPACITY) {
			symphony_note_dropped_trigger();
			return false;
		}
		queue[w % QUEUE_CAPACITY].value = p_value;
		queue[w % QUEUE_CAPACITY].target_frame = p_target_frame;
		write_pos.store(w + 1, std::memory_order_release);
		return true;
	}

	// Audio thread only: fire on the next execute() without touching the SPSC queue.
	void fire_local(float p_value = 1.0f) {
		local_value = p_value;
		local_pending = true;
	}

	bool get_auto_trigger_on_play() const { return auto_trigger_on_play; }

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "TriggerInput";
		desc.category = "I/O";
		desc.outputs.push_back({ "output", SymphonyPinType::TRIGGER, false });
		desc.params.push_back({ "auto_trigger_on_play", 1.0f, 0.0f, 1.0f, 1.0f });
		desc.state_size = sizeof(SymphonyTriggerInput);
		desc.state_align = alignof(SymphonyTriggerInput);
		desc.create_fn = &SymphonyTriggerInput::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		void *mem = p_arena.alloc(sizeof(SymphonyTriggerInput), alignof(SymphonyTriggerInput));
		SymphonyTriggerInput *op = new (mem) SymphonyTriggerInput();
		const Variant *v = p_params.getptr(StringName("auto_trigger_on_play"));
		if (v) {
			op->auto_trigger_on_play = (float)(*v) >= 0.5f;
		}
		return op;
	}
};
