#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"
#include "scene/resources/audio_stream_wav.h"
#include "core/io/resource_loader.h"

// Plays AudioStreamWAV samples with pitch control and loop points.
// Two modes:
//   Reference: reads from AudioStreamWAV resource's internal buffer (zero-copy)
//   Baked: PCM copied into arena at compile time (cache-friendly)
// Only supports 16-bit PCM mono/stereo (most common game audio format).
class SymphonyWavePlayer : public SymphonyOperator {
private:
	// Pin pointers
	const TriggerBuffer *gate_input = nullptr;
	const float *pitch_input = nullptr;
	float *output = nullptr;
	TriggerBuffer *finished_output = nullptr;

	// PCM data (either arena-baked or resource pointer)
	const int16_t *pcm_data = nullptr;
	int32_t pcm_length_samples = 0; // Total samples (per channel)
	bool stereo = false;

	// Reference mode: keep data alive
	Vector<uint8_t> pcm_data_copy; // Holds the PCM bytes when in reference mode

	// Playback state
	double read_pos = 0.0;
	bool playing = false;
	int8_t direction = 1; // 1=forward, -1=backward (for ping-pong)

	// Parameters (set at compile time)
	float base_pitch = 1.0f;
	float source_rate_ratio = 1.0f; // source_mix_rate / graph_mix_rate
	int32_t loop_mode = 0;   // 0=none, 1=forward, 2=ping-pong
	int32_t loop_start = 0;  // In samples
	int32_t loop_end = 0;    // In samples (0 = end of file)
	bool auto_play = true;

	inline float read_sample(int32_t pos) const {
		if (pos < 0 || pos >= pcm_length_samples) {
			return 0.0f;
		}
		if (stereo) {
			// Mix stereo to mono: (L + R) / 2
			return ((float)pcm_data[pos * 2] + (float)pcm_data[pos * 2 + 1]) / 65536.0f;
		}
		return (float)pcm_data[pos] / 32768.0f;
	}

	inline float interpolate(double pos) const {
		int32_t idx = (int32_t)pos;
		float frac = (float)(pos - idx);
		float s0 = read_sample(idx);
		float s1 = read_sample(idx + 1);
		return s0 + (s1 - s0) * frac;
	}

public:
	SymphonyWavePlayer() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		gate_input = (const TriggerBuffer *)p_input_ptrs[0];
		pitch_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
		finished_output = (TriggerBuffer *)p_output_ptrs[1];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		// Check gate trigger
		if (gate_input && gate_input->count > 0) {
			playing = true;
			read_pos = (loop_mode != 0) ? loop_start : 0.0;
			direction = 1;
		}

		if (!playing || !pcm_data) {
			for (int32_t i = 0; i < p_num_frames; i++) {
				output[i] = 0.0f;
			}
			return;
		}

		float pitch = base_pitch * source_rate_ratio;
		if (pitch_input) {
			pitch *= pitch_input[0];
		}

		int32_t effective_end = (loop_end > 0) ? loop_end : pcm_length_samples;

		for (int32_t i = 0; i < p_num_frames; i++) {
			output[i] = interpolate(read_pos);
			read_pos += pitch * direction;

			// Handle end/loop
			if (direction > 0 && read_pos >= effective_end) {
				switch (loop_mode) {
					case 0: // No loop
						playing = false;
						if (finished_output) {
							finished_output->push(i, 1.0f);
						}
						for (int32_t j = i + 1; j < p_num_frames; j++) {
							output[j] = 0.0f;
						}
						return;
					case 1: // Forward loop
						read_pos = loop_start + (read_pos - effective_end);
						break;
					case 2: // Ping-pong
						read_pos = effective_end - (read_pos - effective_end);
						direction = -1;
						break;
				}
			} else if (direction < 0 && read_pos < loop_start) {
				// Ping-pong reverse hit start
				read_pos = loop_start + (loop_start - read_pos);
				direction = 1;
			}
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		constexpr size_t needed = sizeof(double) + sizeof(bool) + sizeof(int8_t);
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		memcpy(p_buffer, &read_pos, sizeof(double));
		memcpy(p_buffer + sizeof(double), &playing, sizeof(bool));
		memcpy(p_buffer + sizeof(double) + sizeof(bool), &direction, sizeof(int8_t));
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		constexpr size_t needed = sizeof(double) + sizeof(bool) + sizeof(int8_t);
		if (p_size < needed) return;
		memcpy(&read_pos, p_buffer, sizeof(double));
		memcpy(&playing, p_buffer + sizeof(double), sizeof(bool));
		memcpy(&direction, p_buffer + sizeof(double) + sizeof(bool), sizeof(int8_t));
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "WavePlayer";
		desc.category = "Generators";
		desc.inputs.push_back({ "gate", SymphonyPinType::TRIGGER, false });
		desc.inputs.push_back({ "pitch", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "output", SymphonyPinType::AUDIO, false });
		desc.outputs.push_back({ "finished", SymphonyPinType::TRIGGER, false });
		desc.params.push_back({ "resource_path", 0.0f, 0.0f, 0.0f, 0.0f });
		desc.params.push_back({ "bake_audio", 0.0f, 0.0f, 1.0f, 1.0f });
		desc.params.push_back({ "loop_mode", 0.0f, 0.0f, 2.0f, 1.0f });
		desc.params.push_back({ "loop_start", 0.0f, 0.0f, 10000000.0f, 1.0f });
		desc.params.push_back({ "loop_end", 0.0f, 0.0f, 10000000.0f, 1.0f });
		desc.params.push_back({ "pitch_scale", 1.0f, 0.01f, 16.0f, 0.01f });
		desc.params.push_back({ "auto_play", 1.0f, 0.0f, 1.0f, 1.0f });
		desc.state_size = sizeof(SymphonyWavePlayer);
		desc.state_align = alignof(SymphonyWavePlayer);
		desc.create_fn = &SymphonyWavePlayer::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyWavePlayer), alignof(SymphonyWavePlayer));
		SymphonyWavePlayer *wp = new (mem) SymphonyWavePlayer();

		// Load resource
		String path;
		if (p_params.has("resource_path")) {
			path = String(p_params["resource_path"]);
		}
		if (path.is_empty()) {
			return wp; // No resource — will output silence
		}

		Ref<AudioStreamWAV> wav = ResourceLoader::load(path, "AudioStreamWAV");
		if (wav.is_null()) {
			return wp;
		}

		// Only support 16-bit PCM
		if (wav->get_format() != AudioStreamWAV::FORMAT_16_BITS) {
			return wp;
		}

		bool bake = p_params.has("bake_audio") ? ((float)p_params["bake_audio"] > 0.5f) : false;
		wp->stereo = wav->is_stereo();
		int channels = wp->stereo ? 2 : 1;
		Vector<uint8_t> raw_data = wav->get_data();
		wp->pcm_length_samples = raw_data.size() / (2 * channels); // 2 bytes per sample per channel

		if (bake) {
			// Copy PCM into arena
			size_t byte_count = raw_data.size();
			void *arena_buf = p_arena.alloc(byte_count, 16);
			memcpy(arena_buf, raw_data.ptr(), byte_count);
			wp->pcm_data = (const int16_t *)arena_buf;
		} else {
			// Reference mode: store data copy to keep pointer alive
			wp->pcm_data_copy = raw_data;
			wp->pcm_data = (const int16_t *)wp->pcm_data_copy.ptr();
		}

		// Parameters
		wp->source_rate_ratio = (float)wav->get_mix_rate() / p_mix_rate;
		wp->base_pitch = p_params.has("pitch_scale") ? (float)p_params["pitch_scale"] : 1.0f;
		wp->loop_mode = p_params.has("loop_mode") ? (int32_t)(float)p_params["loop_mode"] : 0;
		wp->loop_start = p_params.has("loop_start") ? (int32_t)(float)p_params["loop_start"] : 0;
		wp->loop_end = p_params.has("loop_end") ? (int32_t)(float)p_params["loop_end"] : 0;
		wp->auto_play = p_params.has("auto_play") ? ((float)p_params["auto_play"] > 0.5f) : true;
		wp->playing = wp->auto_play;

		// Use WAV's own loop points if ours are default
		if (wp->loop_mode == 0 && wav->get_loop_mode() != AudioStreamWAV::LOOP_DISABLED) {
			wp->loop_mode = (wav->get_loop_mode() == AudioStreamWAV::LOOP_PINGPONG) ? 2 : 1;
			wp->loop_start = wav->get_loop_begin();
			wp->loop_end = wav->get_loop_end();
		}

		return wp;
	}
};
