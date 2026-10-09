#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_trigger.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "core/variant/variant.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/resources/audio/audio_stream_wav.h"
#include "servers/audio/audio_server.h"
#include <cmath>

// Plays AudioStreamWAV samples with pitch control and loop points.
// Two modes:
//   Reference: reads from AudioStreamWAV resource's internal buffer (zero-copy)
//   Baked: PCM copied into arena at compile time (cache-friendly)
// All AudioStreamWAV formats are decoded to PCM16 at create time (main thread):
// 8-bit widens; IMA ADPCM / QOA render via instantiate_playback()->mix().
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

	inline float read_sample_raw(int32_t pos) const {
		if (pos < 0 || pos >= pcm_length_samples) {
			return 0.0f;
		}
		if (stereo) {
			return ((float)pcm_data[pos * 2] + (float)pcm_data[pos * 2 + 1]) / 65536.0f;
		}
		return (float)pcm_data[pos] / 32768.0f;
	}

	// Map an absolute sample index into the active loop/region for interpolation taps.
	inline int32_t map_loop_index(int32_t idx, int32_t p_loop_start, int32_t p_loop_end) const {
		if (loop_mode == 0) {
			return idx;
		}
		const int32_t start = p_loop_start;
		const int32_t end = p_loop_end;
		const int32_t len = end - start;
		if (len <= 0) {
			return CLAMP(idx, 0, MAX(0, pcm_length_samples - 1));
		}
		if (loop_mode == 1) {
			// Forward wrap: past end → start + (i − end)
			if (idx >= end) {
				int32_t over = idx - end;
				return start + (over % len);
			}
			if (idx < start) {
				int32_t under = start - idx;
				int32_t back = under % len;
				return end - (back == 0 ? len : back);
			}
			return idx;
		}
		// Ping-pong: reflect across [start, end).
		int32_t period = len * 2;
		if (period <= 0) {
			return start;
		}
		int32_t rel = idx - start;
		// Bring rel into a non-negative range before mod.
		rel %= period;
		if (rel < 0) {
			rel += period;
		}
		if (rel < len) {
			return start + rel;
		}
		return end - 1 - (rel - len);
	}

	inline float fetch_looped(int32_t idx, int32_t p_loop_start, int32_t p_loop_end) const {
		if (loop_mode == 0) {
			return read_sample_raw(idx);
		}
		return read_sample_raw(map_loop_index(idx, p_loop_start, p_loop_end));
	}

	// 4-point Catmull-Rom (Hermite) with loop-aware taps.
	inline float interpolate(double pos, int32_t p_loop_start, int32_t p_loop_end) const {
		int32_t idx = (int32_t)std::floor(pos);
		float frac = (float)(pos - (double)idx);
		float ym1 = fetch_looped(idx - 1, p_loop_start, p_loop_end);
		float y0 = fetch_looped(idx, p_loop_start, p_loop_end);
		float y1 = fetch_looped(idx + 1, p_loop_start, p_loop_end);
		float y2 = fetch_looped(idx + 2, p_loop_start, p_loop_end);
		// Catmull-Rom: 0.5 * (2*y0 + (-ym1+y1)*t + (2*ym1-5*y0+4*y1-y2)*t^2 + (-ym1+3*y0-3*y1+y2)*t^3)
		float t = frac;
		float t2 = t * t;
		float t3 = t2 * t;
		return 0.5f * ((2.0f * y0) + (-ym1 + y1) * t + (2.0f * ym1 - 5.0f * y0 + 4.0f * y1 - y2) * t2 + (-ym1 + 3.0f * y0 - 3.0f * y1 + y2) * t3);
	}

public:
	SymphonyWavePlayer() {}

	// Exposed so PreparedGraphPackage can watch one-shot completion.
	TriggerBuffer *get_finished_output() const { return finished_output; }

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		gate_input = (const TriggerBuffer *)p_input_ptrs[0];
		pitch_input = (const float *)p_input_ptrs[1];
		output = (float *)p_output_ptrs[0];
		finished_output = (TriggerBuffer *)p_output_ptrs[1];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float pitch = base_pitch * source_rate_ratio;
		if (pitch_input) {
			pitch *= pitch_input[0];
		}

		int32_t effective_end = (loop_end > 0) ? loop_end : pcm_length_samples;
		int32_t next_trig = 0;

		for (int32_t i = 0; i < p_num_frames; i++) {
			// Honor gate triggers at exact sample offsets (plan §9).
			while (gate_input && next_trig < gate_input->count &&
					gate_input->events[next_trig].sample_offset <= i) {
				playing = true;
				read_pos = (loop_mode != 0) ? (double)loop_start : 0.0;
				direction = 1;
				next_trig++;
			}

			if (!playing || !pcm_data) {
				output[i] = 0.0f;
				continue;
			}

			output[i] = interpolate(read_pos, loop_start, effective_end);
			read_pos += pitch * direction;

			// Handle end/loop
			if (direction > 0 && read_pos >= effective_end) {
				switch (loop_mode) {
					case 0: // No loop
						playing = false;
						if (finished_output) {
							if (!finished_output->push(i, 1.0f)) {
								symphony_note_dropped_trigger();
							}
						}
						break;
					case 1: // Forward loop
						read_pos = (double)loop_start + (read_pos - (double)effective_end);
						break;
					case 2: // Ping-pong
						read_pos = (double)effective_end - (read_pos - (double)effective_end);
						direction = -1;
						break;
				}
			} else if (direction < 0 && read_pos < loop_start) {
				// Ping-pong reverse hit start
				read_pos = (double)loop_start + ((double)loop_start - read_pos);
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

	// Decode any AudioStreamWAV format into interleaved PCM16 (main thread only).
	// 8-bit widens; IMA ADPCM / QOA mix via playback at 1:1 stream-rate frames.
	static bool decode_to_pcm16(const Ref<AudioStreamWAV> &p_wav, Vector<uint8_t> &r_pcm16, bool &r_stereo, int32_t &r_length_samples) {
		if (p_wav.is_null()) {
			return false;
		}
		r_stereo = p_wav->is_stereo();
		const int channels = r_stereo ? 2 : 1;
		const AudioStreamWAV::Format fmt = p_wav->get_format();

		if (fmt == AudioStreamWAV::FORMAT_16_BITS) {
			r_pcm16 = p_wav->get_data();
			if (r_pcm16.is_empty()) {
				r_length_samples = 0;
				return true;
			}
			if ((r_pcm16.size() % (2 * channels)) != 0) {
				return false;
			}
			r_length_samples = (int32_t)(r_pcm16.size() / (2 * channels));
			return true;
		}

		if (fmt == AudioStreamWAV::FORMAT_8_BITS) {
			Vector<uint8_t> raw = p_wav->get_data();
			if (raw.is_empty()) {
				r_pcm16.clear();
				r_length_samples = 0;
				return true;
			}
			if ((raw.size() % channels) != 0) {
				return false;
			}
			r_length_samples = (int32_t)(raw.size() / channels);
			r_pcm16.resize((size_t)r_length_samples * (size_t)channels * 2);
			const int8_t *src = reinterpret_cast<const int8_t *>(raw.ptr());
			int16_t *dst = reinterpret_cast<int16_t *>(r_pcm16.ptrw());
			const int n = r_length_samples * channels;
			for (int i = 0; i < n; i++) {
				dst[i] = (int16_t)src[i] << 8;
			}
			return true;
		}

		if (fmt != AudioStreamWAV::FORMAT_IMA_ADPCM && fmt != AudioStreamWAV::FORMAT_QOA) {
			return false;
		}

		Ref<AudioStreamPlayback> pb = p_wav->instantiate_playback();
		if (pb.is_null()) {
			return false;
		}
		AudioServer *server = AudioServer::get_singleton();
		if (server == nullptr) {
			return false;
		}
		const float stream_rate = (float)p_wav->get_mix_rate();
		const float server_rate = server->get_mix_rate();
		if (!(stream_rate > 0.0f) || !(server_rate > 0.0f)) {
			return false;
		}
		// mix_increment ∝ stream_rate * rate_scale / server_rate; choose scale for 1:1 frames.
		const float rate_scale = server_rate / stream_rate;
		const double length_sec = p_wav->get_length();
		const int total = MAX(1, (int)Math::ceil(length_sec * (double)stream_rate));

		pb->start(0.0);
		Vector<AudioFrame> frames;
		frames.resize(total);
		int filled = 0;
		while (filled < total) {
			const int want = MIN(512, total - filled);
			const int got = pb->mix(frames.ptrw() + filled, rate_scale, want);
			if (got <= 0) {
				break;
			}
			filled += got;
		}
		if (filled <= 0) {
			return false;
		}

		r_length_samples = filled;
		r_pcm16.resize((size_t)filled * (size_t)channels * 2);
		int16_t *dst = reinterpret_cast<int16_t *>(r_pcm16.ptrw());
		for (int i = 0; i < filled; i++) {
			if (r_stereo) {
				dst[i * 2] = (int16_t)CLAMP(frames[i].left * 32767.0f, -32768.0f, 32767.0f);
				dst[i * 2 + 1] = (int16_t)CLAMP(frames[i].right * 32767.0f, -32768.0f, 32767.0f);
			} else {
				const float mono = 0.5f * (frames[i].left + frames[i].right);
				dst[i] = (int16_t)CLAMP(mono * 32767.0f, -32768.0f, 32767.0f);
			}
		}
		return true;
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
		// Baked PCM budget: up to 5 seconds of stereo 16-bit at 48kHz = 960,000 bytes.
		// Files larger than this will fall back to reference mode (no arena copy).
		desc.extra_arena_bytes = 960000 + 32;
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

		// Prefer load_from_file for on-disk .wav; ResourceLoader for imported .res/.tres.
		Ref<AudioStreamWAV> wav;
		const String ext = path.get_extension().to_lower();
		if (ext == "wav") {
			Dictionary options;
			wav = AudioStreamWAV::load_from_file(path, options);
		}
		if (wav.is_null()) {
			wav = ResourceLoader::load(path, "AudioStreamWAV");
		}
		if (wav.is_null()) {
			ERR_PRINT_ONCE(vformat("SymphonyWavePlayer: failed to load AudioStreamWAV '%s'.", path));
			return wp;
		}

		Vector<uint8_t> pcm16;
		bool stereo = false;
		int32_t length_samples = 0;
		if (!decode_to_pcm16(wav, pcm16, stereo, length_samples)) {
			ERR_PRINT_ONCE(vformat("SymphonyWavePlayer: failed to decode '%s' (unsupported or corrupt AudioStreamWAV).", path));
			return wp;
		}
		if (length_samples <= 0 || pcm16.is_empty()) {
			ERR_PRINT_ONCE(vformat("SymphonyWavePlayer: decoded empty PCM from '%s'.", path));
			return wp;
		}

		bool bake = p_params.has("bake_audio") ? ((float)p_params["bake_audio"] > 0.5f) : false;
		wp->stereo = stereo;
		wp->pcm_length_samples = length_samples;

		if (bake) {
			// Copy PCM into arena — but only if it fits the arena budget.
			// Budget: 960,000 bytes (5s stereo 16-bit at 48kHz).
			// Larger files fall back to reference mode to avoid arena overflow.
			size_t byte_count = pcm16.size();
			if (byte_count <= 960000) {
				void *arena_buf = p_arena.alloc(byte_count, 16);
				if (arena_buf) {
					memcpy(arena_buf, pcm16.ptr(), byte_count);
					wp->pcm_data = (const int16_t *)arena_buf;
				} else {
					// Arena alloc failed — fall back to reference mode
					wp->pcm_data_copy = pcm16;
					wp->pcm_data = (const int16_t *)wp->pcm_data_copy.ptr();
				}
			} else {
				// File too large for arena budget — use reference mode instead
				wp->pcm_data_copy = pcm16;
				wp->pcm_data = (const int16_t *)wp->pcm_data_copy.ptr();
			}
		} else {
			// Reference mode: store data copy to keep pointer alive
			wp->pcm_data_copy = pcm16;
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
