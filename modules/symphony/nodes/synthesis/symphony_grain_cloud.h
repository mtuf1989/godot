#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_seed.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "../../core/symphony_checked_math.h"
#include "../../core/symphony_trigger.h"
#include "../../core/symphony_runtime_metrics.h"
#include "../../core/shared_pcm_cache.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"

#include <limits>

// GrainCloud: Internal micro-scheduler managing overlapping grains.
// Supports live audio input granulation and source-buffer mode.
//
// Each grain is a windowed segment of the source, with:
// - size 2–500 ms, density up to 500/s
// - stochastic scheduling (Poisson-like) and/or external triggers
// - pitch_range_st (±semitones), reverse_prob, amp/position randomness
// - optional overlap normalization (on by default)
// - window shapes: hann | tukey | expodec | rexpodec
//
// Trigger input:
// - trigger_only=false (default): triggers spawn in addition to the Poisson scheduler.
// - trigger_only=true: scheduler off; grains spawn only from triggers.
// - Full pool: trigger spawns steal the oldest grain; scheduler drops and counts.
//
// Max 32 concurrent grains. Mono output — use a downstream Panner for spread.
class SymphonyGrainCloud : public SymphonyOperator {
private:
	static constexpr int32_t MAX_GRAINS = 32;
	static constexpr int32_t WINDOW_TABLE_SIZE = 1024;

	enum WindowShape : int32_t {
		WINDOW_HANN = 0,
		WINDOW_TUKEY = 1,
		WINDOW_EXPODEC = 2,
		WINDOW_REXPODEC = 3,
	};

	struct Grain {
		bool active = false;
		int32_t start_sample = 0;
		int32_t length_samples = 0;
		int32_t current_sample = 0;
		float playback_rate = 1.0f;
		float read_pos = 0.0f;
		float amplitude = 1.0f;
	};

	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const TriggerBuffer *SYMPHONY_RESTRICT trigger_in = nullptr;
	const float *SYMPHONY_RESTRICT grain_size_input = nullptr;
	const float *SYMPHONY_RESTRICT density_input = nullptr;
	const float *SYMPHONY_RESTRICT position_input = nullptr;
	const float *SYMPHONY_RESTRICT pitch_range_input = nullptr;
	const float *SYMPHONY_RESTRICT scan_speed_input = nullptr;
	const float *SYMPHONY_RESTRICT amp_rand_input = nullptr;
	const float *SYMPHONY_RESTRICT reverse_prob_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	float *capture_buffer = nullptr;
	int32_t capture_size = 0;
	int32_t capture_write = 0;

	bool source_pcm_loaded = false;
	int32_t source_pcm_length = 0;
	bool using_shared_pcm = false;
	StringName shared_pcm_key;

	Grain grains[MAX_GRAINS];

	float schedule_counter = 0.0f;
	float last_density = -1.0f;
	float auto_position = 0.0f;

	float *window_table = nullptr;
	int32_t window_size = 0;
	float window_energy = 0.375f; // E[w²] of baked window

	float mix_rate = 44100.0f;
	float default_grain_size_ms = 50.0f;
	float default_density = 8.0f;
	float default_position = 0.5f;
	float default_pitch_range_st = 0.0f;
	float default_scan_speed = 0.0f;
	float default_amp_randomness = 0.0f;
	float default_position_randomness = 0.1f;
	float default_reverse_prob = 0.0f;
	bool trigger_only = false;
	bool overlap_normalize = true;

	uint32_t rng_state = 1;

	inline uint32_t xorshift32() {
		rng_state ^= rng_state << 13;
		rng_state ^= rng_state >> 17;
		rng_state ^= rng_state << 5;
		return rng_state;
	}

	inline float random_float() {
		return (float)(xorshift32() & 0x7FFFFF) / (float)0x800000;
	}

	inline float window_at(float phase) const {
		float pos = phase * (float)(window_size - 1);
		int32_t idx = (int32_t)pos;
		if (idx >= window_size - 1) {
			return 0.0f;
		}
		float frac = pos - (float)idx;
		return window_table[idx] + frac * (window_table[idx + 1] - window_table[idx]);
	}

	inline float read_capture(float pos) const {
		int32_t buf_len = source_pcm_loaded ? source_pcm_length : capture_size;
		while (pos < 0.0f) {
			pos += (float)buf_len;
		}
		while (pos >= (float)buf_len) {
			pos -= (float)buf_len;
		}
		int32_t idx = (int32_t)pos;
		float frac = pos - (float)idx;
		int32_t next = (idx + 1) % buf_len;
		return capture_buffer[idx] + frac * (capture_buffer[next] - capture_buffer[idx]);
	}

	static float eval_window(WindowShape shape, float p) {
		p = CLAMP(p, 0.0f, 1.0f);
		switch (shape) {
			case WINDOW_TUKEY: {
				const float alpha = 0.5f;
				const float edge = alpha * 0.5f;
				if (p < edge) {
					return 0.5f * (1.0f - Math::cos(Math::PI * p / edge));
				}
				if (p > 1.0f - edge) {
					return 0.5f * (1.0f - Math::cos(Math::PI * (1.0f - p) / edge));
				}
				return 1.0f;
			}
			case WINDOW_EXPODEC: {
				float w = Math::exp(-6.0f * p);
				if (p < 0.04f) {
					w *= p / 0.04f;
				}
				return w;
			}
			case WINDOW_REXPODEC: {
				float w = Math::exp(-6.0f * (1.0f - p));
				if (p > 0.96f) {
					w *= (1.0f - p) / 0.04f;
				}
				return w;
			}
			case WINDOW_HANN:
			default:
				return 0.5f * (1.0f - Math::cos(Math::TAU * p));
		}
	}

public:
	SymphonyGrainCloud() {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		trigger_in = (const TriggerBuffer *)p_input_ptrs[1];
		grain_size_input = (const float *)p_input_ptrs[2];
		density_input = (const float *)p_input_ptrs[3];
		position_input = (const float *)p_input_ptrs[4];
		pitch_range_input = (const float *)p_input_ptrs[5];
		scan_speed_input = (const float *)p_input_ptrs[6];
		amp_rand_input = (const float *)p_input_ptrs[7];
		reverse_prob_input = (const float *)p_input_ptrs[8];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void cleanup() override {
		if (using_shared_pcm && shared_pcm_key != StringName()) {
			SharedPCMCache *cache = SharedPCMCache::get_singleton();
			if (cache) {
				cache->release(shared_pcm_key);
			}
			using_shared_pcm = false;
			capture_buffer = nullptr;
		}
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float grain_size_ms = grain_size_input ? *grain_size_input : default_grain_size_ms;
		float density = density_input ? *density_input : default_density;
		float position = position_input ? *position_input : default_position;
		float pitch_range = pitch_range_input ? *pitch_range_input : default_pitch_range_st;
		float scan_speed = scan_speed_input ? *scan_speed_input : default_scan_speed;
		float amp_rand = amp_rand_input ? *amp_rand_input : default_amp_randomness;
		float reverse_prob = reverse_prob_input ? *reverse_prob_input : default_reverse_prob;

		grain_size_ms = CLAMP(grain_size_ms, 2.0f, 500.0f);
		density = CLAMP(density, 0.0f, 500.0f);
		position = CLAMP(position, 0.0f, 1.0f);
		pitch_range = CLAMP(pitch_range, 0.0f, 24.0f);
		scan_speed = CLAMP(scan_speed, 0.0f, 4.0f);
		amp_rand = CLAMP(amp_rand, 0.0f, 6.0f);
		reverse_prob = CLAMP(reverse_prob, 0.0f, 1.0f);

		int32_t grain_length = (int32_t)(grain_size_ms * mix_rate * 0.001f);
		if (grain_length < 1) {
			grain_length = 1;
		}

		// N7: finite interval; density 0 never schedules (no NaN/inf).
		const float avg_interval = (density > 0.001f) ? (mix_rate / density) : 1.0e9f;
		if (last_density < 0.0f) {
			last_density = density;
		} else {
			const bool crossed_zero = (last_density <= 0.001f) != (density <= 0.001f);
			const bool jumped = (last_density > 0.001f && density > 0.001f) &&
					(density > last_density * 2.0f || density < last_density * 0.5f);
			if (crossed_zero || jumped) {
				// Reschedule promptly so density ramps are audible within a few intervals.
				if (density > 0.001f && last_density <= 0.001f) {
					schedule_counter = 0.0f; // fire on the next sample
				} else {
					const float u = MAX(random_float(), 0.01f);
					schedule_counter = MIN(schedule_counter, avg_interval * u);
				}
			}
			last_density = density;
		}

		int32_t effective_size = source_pcm_loaded ? source_pcm_length : capture_size;
		float scan_increment = scan_speed / (float)MAX(1, effective_size);

		float size_s = grain_size_ms * 0.001f;
		float overlap_gain = 1.0f;
		if (overlap_normalize) {
			// √(density·size·E[w²]) with a small boost so ~1× vs ~8× overlap stays within ~1.5 dB.
			const float expected = density * size_s * window_energy * 1.65f;
			overlap_gain = 1.0f / Math::sqrt(MAX(1.0f, expected));
		}

		int32_t current_frame = 0;
		int32_t trigger_idx = 0;

		while (current_frame < p_num_frames) {
			int32_t segment_end = p_num_frames;
			bool trigger_at_boundary = false;

			if (trigger_in && trigger_idx < trigger_in->count) {
				int32_t trig_offset = trigger_in->events[trigger_idx].sample_offset;
				if (trig_offset <= current_frame) {
					(void)trigger_in->events[trigger_idx].value;
					trigger_idx++;
					float eff_pos = position + auto_position;
					if (eff_pos >= 1.0f) {
						eff_pos -= 1.0f;
					}
					spawn_grain(grain_length, eff_pos, pitch_range, amp_rand, reverse_prob, true);
					continue;
				} else {
					segment_end = trig_offset;
					trigger_at_boundary = true;
				}
			}

			for (int32_t i = current_frame; i < segment_end; i++) {
				if (!source_pcm_loaded) {
					capture_buffer[capture_write] = audio_in ? audio_in[i] : 0.0f;
					capture_write = (capture_write + 1) % capture_size;
				}

				if (scan_speed > 0.001f) {
					auto_position += scan_increment;
					if (auto_position >= 1.0f) {
						auto_position -= 1.0f;
					}
				}

				float effective_position = position + auto_position;
				if (effective_position >= 1.0f) {
					effective_position -= 1.0f;
				}

				if (!trigger_only && density > 0.001f) {
					schedule_counter -= 1.0f;
					if (schedule_counter <= 0.0f) {
						spawn_grain(grain_length, effective_position, pitch_range, amp_rand, reverse_prob, false);
						float u = random_float();
						if (u < 0.0001f) {
							u = 0.0001f;
						}
						schedule_counter = -Math::log(u) * avg_interval;
						if (!Math::is_finite(schedule_counter) || schedule_counter < 0.0f) {
							schedule_counter = avg_interval;
						}
					}
				}

				float output = 0.0f;
				for (int32_t g = 0; g < MAX_GRAINS; g++) {
					Grain &grain = grains[g];
					if (!grain.active) {
						continue;
					}

					float phase = (float)grain.current_sample / (float)grain.length_samples;
					float window = window_at(phase);
					float read_sample_pos = (float)grain.start_sample + grain.read_pos;
					output += read_capture(read_sample_pos) * window * grain.amplitude;

					grain.read_pos += grain.playback_rate;
					grain.current_sample++;
					if (grain.current_sample >= grain.length_samples) {
						grain.active = false;
					}
				}

				audio_out[i] = output * overlap_gain;
			}

			if (trigger_at_boundary) {
				float eff_pos = position + auto_position;
				if (eff_pos >= 1.0f) {
					eff_pos -= 1.0f;
				}
				spawn_grain(grain_length, eff_pos, pitch_range, amp_rand, reverse_prob, true);
				trigger_idx++;
			}

			current_frame = segment_end;
		}
	}

	void spawn_grain(int32_t length_samples, float position, float pitch_range_st, float amp_rand, float reverse_prob, bool allow_steal) {
		int32_t slot = -1;
		int32_t oldest = -1;
		int32_t oldest_progress = -1;
		for (int32_t g = 0; g < MAX_GRAINS; g++) {
			if (!grains[g].active) {
				slot = g;
				break;
			}
			if (grains[g].current_sample > oldest_progress) {
				oldest_progress = grains[g].current_sample;
				oldest = g;
			}
		}
		if (slot < 0) {
			if (allow_steal && oldest >= 0) {
				slot = oldest;
			} else {
				symphony_note_dropped_grain();
				return;
			}
		}

		Grain &grain = grains[slot];
		grain.active = true;
		grain.length_samples = length_samples;
		grain.current_sample = 0;
		grain.read_pos = 0.0f;

		float pitch_deviation = (random_float() - 0.5f) * 2.0f * pitch_range_st;
		grain.playback_rate = Math::pow(2.0f, pitch_deviation / 12.0f);
		if (reverse_prob > 0.0f && random_float() < reverse_prob) {
			grain.playback_rate = -grain.playback_rate;
		}

		float jitter = (random_float() - 0.5f) * 2.0f * default_position_randomness;
		float final_pos = CLAMP(position + jitter, 0.0f, 1.0f);

		if (source_pcm_loaded) {
			grain.start_sample = (int32_t)(final_pos * (float)MAX(1, source_pcm_length - length_samples));
			grain.start_sample = CLAMP(grain.start_sample, 0, source_pcm_length - 1);
			if (grain.playback_rate < 0.0f) {
				grain.read_pos = (float)(length_samples - 1);
			}
		} else {
			// N17: keep the grain read behind the write head for |rate| > 1.
			const float abs_rate = Math::abs(grain.playback_rate);
			int32_t min_behind = (int32_t)((float)length_samples * MAX(0.0f, abs_rate - 1.0f)) + 1;
			int32_t offset_behind = (int32_t)((1.0f - final_pos) * (float)MAX(1, capture_size - length_samples));
			offset_behind = MAX(offset_behind, min_behind);
			offset_behind = MIN(offset_behind, capture_size - 1);
			DEV_ASSERT(offset_behind >= min_behind);
			grain.start_sample = (capture_write - offset_behind + capture_size) % capture_size;
			if (grain.playback_rate < 0.0f) {
				grain.read_pos = (float)(length_samples - 1);
			}
		}

		if (amp_rand > 0.001f) {
			float amp_deviation_db = (random_float() - 0.5f) * 2.0f * amp_rand;
			grain.amplitude = Math::pow(10.0f, amp_deviation_db / 20.0f);
		} else {
			grain.amplitude = 1.0f;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		size_t needed = sizeof(int32_t) + sizeof(float) * 3 + sizeof(uint32_t) + sizeof(Grain) * MAX_GRAINS;
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}

		size_t offset = 0;
		memcpy(p_buffer + offset, &capture_write, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(p_buffer + offset, &schedule_counter, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &auto_position, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &last_density, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &rng_state, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		memcpy(p_buffer + offset, grains, sizeof(Grain) * MAX_GRAINS);
		offset += sizeof(Grain) * MAX_GRAINS;
		return offset;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) * 3 + sizeof(uint32_t) + sizeof(Grain) * MAX_GRAINS;
		if (p_size < needed) {
			return;
		}

		size_t offset = 0;
		memcpy(&capture_write, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
		memcpy(&schedule_counter, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&auto_position, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&last_density, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&rng_state, p_buffer + offset, sizeof(uint32_t));
		offset += sizeof(uint32_t);
		memcpy(grains, p_buffer + offset, sizeof(Grain) * MAX_GRAINS);
	}

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "GrainCloud";
		desc.category = "Synthesis";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, false });
		desc.inputs.push_back({ "trigger", SymphonyPinType::TRIGGER, false });
		desc.inputs.push_back({ "grain_size_ms", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "density", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "position", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "pitch_range_st", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "scan_speed", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "amp_randomness", SymphonyPinType::FLOAT, false });
		desc.inputs.push_back({ "reverse_prob", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "grain_size_ms", 50.0f, 2.0f, 500.0f, 1.0f });
		desc.params.push_back({ "density", 8.0f, 0.0f, 500.0f, 0.1f });
		desc.params.push_back({ "position", 0.5f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "pitch_range_st", 0.0f, 0.0f, 24.0f, 0.1f });
		desc.params.push_back({ "scan_speed", 0.0f, 0.0f, 4.0f, 0.01f });
		desc.params.push_back({ "amp_randomness", 0.0f, 0.0f, 6.0f, 0.1f });
		desc.params.push_back({ "position_randomness", 0.1f, 0.0f, 1.0f, 0.01f });
		desc.params.push_back({ "reverse_prob", 0.0f, 0.0f, 1.0f, 0.01f });
		{
			ParamDescriptor win;
			win.name = "window_shape";
			win.default_value = 0.0f;
			win.min_value = 0.0f;
			win.max_value = 3.0f;
			win.step = 1.0f;
			win.value_type = ParamValueType::ENUM;
			win.enum_values.push_back("hann");
			win.enum_values.push_back("tukey");
			win.enum_values.push_back("expodec");
			win.enum_values.push_back("rexpodec");
			desc.params.push_back(win);
		}
		{
			ParamDescriptor onorm;
			onorm.name = "overlap_normalize";
			onorm.default_value = 1.0f;
			onorm.min_value = 0.0f;
			onorm.max_value = 1.0f;
			onorm.step = 1.0f;
			onorm.value_type = ParamValueType::BOOL;
			desc.params.push_back(onorm);
		}
		desc.params.push_back({ "trigger_only", 0.0f, 0.0f, 1.0f, 1.0f });
		desc.params.push_back({ "seed", 1.0f, 0.0f, (float)SYMPHONY_SEED_PARAM_MAX, 1.0f });
		desc.params.push_back({ "capture_seconds", 2.0f, 1.0f, 10.0f, 1.0f });
		desc.state_size = sizeof(SymphonyGrainCloud);
		desc.state_align = alignof(SymphonyGrainCloud);
		desc.extra_arena_bytes = 0;
		desc.extra_arena_bytes_fn = &SymphonyGrainCloud::calculate_arena_bytes;
		desc.cost_per_sample = 24.0f;
		desc.extra_cost_fn = &SymphonyGrainCloud::extra_cost;
		desc.create_fn = &SymphonyGrainCloud::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	[[nodiscard]] static float extra_cost(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		float density = p_params.has("density") ? (float)p_params["density"] : 8.0f;
		density = CLAMP(density, 0.0f, 500.0f);
		float grain_ms = p_params.has("grain_size_ms") ? (float)p_params["grain_size_ms"] : 50.0f;
		grain_ms = CLAMP(grain_ms, 2.0f, 500.0f);
		const float concurrent = MIN(32.0f, density * grain_ms * 0.001f);
		const float surplus = MAX(0.0f, concurrent - 4.0f);
		return surplus * (float)SYMPHONY_MICRO_BLOCK_SIZE * 3.0f;
	}

	static size_t calculate_arena_bytes(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		static constexpr size_t WINDOW_BYTES = sizeof(float) * WINDOW_TABLE_SIZE;
		size_t offset = 0;

		bool shared_only = false;
		if (p_params.has("source_pcm_cache_key") && p_params.has("source_pcm_ptr") && p_params.has("source_pcm_length")) {
			String key = p_params.has("source_pcm_cache_key") ? String(p_params["source_pcm_cache_key"]) : "";
			shared_only = !key.is_empty();
		}

		if (!shared_only) {
			float capture_sec = p_params.has("capture_seconds") ? (float)p_params["capture_seconds"] : 2.0f;
			capture_sec = CLAMP(capture_sec, 1.0f, 10.0f);
			float rate = p_mix_rate > 1.0f ? p_mix_rate : 48000.0f;
			int32_t capture_samples = (int32_t)(capture_sec * rate);
			if (!SymphonyCheckedMath::bump(offset, sizeof(float) * (size_t)capture_samples, 32)) {
				return std::numeric_limits<size_t>::max();
			}
		}
		if (!SymphonyCheckedMath::bump(offset, WINDOW_BYTES, 32)) {
			return std::numeric_limits<size_t>::max();
		}
		return offset;
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		void *mem = p_arena.alloc(sizeof(SymphonyGrainCloud), alignof(SymphonyGrainCloud));
		if (!mem) {
			return nullptr;
		}
		SymphonyGrainCloud *gc = new (mem) SymphonyGrainCloud();

		gc->mix_rate = p_mix_rate;
		gc->default_grain_size_ms = p_params.has("grain_size_ms") ? (float)p_params["grain_size_ms"] : 50.0f;
		gc->default_density = p_params.has("density") ? (float)p_params["density"] : 8.0f;
		gc->default_position = p_params.has("position") ? (float)p_params["position"] : 0.5f;
		// Prefer pitch_range_st; accept legacy pitch_randomness (was 0–0.5 st scale).
		if (p_params.has("pitch_range_st")) {
			gc->default_pitch_range_st = (float)p_params["pitch_range_st"];
		} else if (p_params.has("pitch_randomness")) {
			gc->default_pitch_range_st = (float)p_params["pitch_randomness"];
		} else {
			gc->default_pitch_range_st = 0.0f;
		}
		gc->default_pitch_range_st = CLAMP(gc->default_pitch_range_st, 0.0f, 24.0f);
		gc->default_scan_speed = p_params.has("scan_speed") ? (float)p_params["scan_speed"] : 0.0f;
		gc->default_amp_randomness = p_params.has("amp_randomness") ? (float)p_params["amp_randomness"] : 0.0f;
		gc->default_position_randomness = p_params.has("position_randomness") ? (float)p_params["position_randomness"] : 0.1f;
		gc->default_reverse_prob = p_params.has("reverse_prob") ? (float)p_params["reverse_prob"] : 0.0f;
		gc->trigger_only = p_params.has("trigger_only") ? ((float)p_params["trigger_only"] >= 0.5f) : false;
		gc->overlap_normalize = p_params.has("overlap_normalize") ? ((float)p_params["overlap_normalize"] >= 0.5f) : true;

		const SymphonyCompileContext &ctx = symphony_compile_context();
		const uint32_t authored = symphony_authored_seed(p_params, 1u);
		gc->rng_state = symphony_node_seed(ctx.voice_seed, ctx.node_id, authored, false);
		if (gc->rng_state == 0u) {
			gc->rng_state = 1u;
		}

		float capture_sec = p_params.has("capture_seconds") ? (float)p_params["capture_seconds"] : 2.0f;
		capture_sec = CLAMP(capture_sec, 1.0f, 10.0f);
		if (OS::get_singleton()->has_feature("web")) {
			capture_sec = MIN(capture_sec, 4.0f);
		}

		gc->capture_size = (int32_t)(capture_sec * p_mix_rate);
		gc->capture_write = 0;

		bool shared_pcm_acquired = false;
		if (p_params.has("source_pcm_ptr") && p_params.has("source_pcm_length")) {
			int64_t ptr_val = (int64_t)p_params["source_pcm_ptr"];
			const float *source_data = reinterpret_cast<const float *>(ptr_val);
			int32_t pcm_len = (int32_t)(float)p_params["source_pcm_length"];

			if (source_data && pcm_len > 0) {
				int32_t use_len = (pcm_len < gc->capture_size) ? pcm_len : gc->capture_size;

				if (p_params.has("source_pcm_cache_key")) {
					StringName cache_key = StringName(String(p_params["source_pcm_cache_key"]));
					SharedPCMCache *pcm_cache = SharedPCMCache::get_singleton();
					if (pcm_cache && cache_key != StringName()) {
						const float *shared_buf = pcm_cache->acquire(cache_key, source_data, use_len);
						if (shared_buf) {
							gc->capture_buffer = const_cast<float *>(shared_buf);
							gc->using_shared_pcm = true;
							gc->shared_pcm_key = cache_key;
							gc->source_pcm_loaded = true;
							gc->source_pcm_length = use_len;
							shared_pcm_acquired = true;
						}
					}
				}

				if (!shared_pcm_acquired) {
					gc->capture_buffer = (float *)p_arena.alloc(sizeof(float) * gc->capture_size, 32);
					if (!gc->capture_buffer) {
						return nullptr;
					}
					memset(gc->capture_buffer, 0, sizeof(float) * gc->capture_size);
					memcpy(gc->capture_buffer, source_data, sizeof(float) * use_len);
					gc->source_pcm_loaded = true;
					gc->source_pcm_length = use_len;
				}
			}
		}

		if (!gc->source_pcm_loaded && !shared_pcm_acquired) {
			gc->capture_buffer = (float *)p_arena.alloc(sizeof(float) * gc->capture_size, 32);
			if (!gc->capture_buffer) {
				return nullptr;
			}
			memset(gc->capture_buffer, 0, sizeof(float) * gc->capture_size);
		}

		WindowShape shape = WINDOW_HANN;
		if (p_params.has("window_shape")) {
			shape = (WindowShape)CLAMP((int32_t)(float)p_params["window_shape"], 0, 3);
		}

		gc->window_size = WINDOW_TABLE_SIZE;
		gc->window_table = (float *)p_arena.alloc(sizeof(float) * gc->window_size, 32);
		if (!gc->window_table) {
			return nullptr;
		}
		double energy_acc = 0.0;
		for (int32_t i = 0; i < gc->window_size; i++) {
			float phase = (float)i / (float)(gc->window_size - 1);
			float w = eval_window(shape, phase);
			gc->window_table[i] = w;
			energy_acc += (double)w * (double)w;
		}
		gc->window_energy = (float)(energy_acc / (double)gc->window_size);
		if (gc->window_energy < 1e-6f) {
			gc->window_energy = 0.375f;
		}

		for (int32_t g = 0; g < MAX_GRAINS; g++) {
			gc->grains[g].active = false;
		}

		gc->auto_position = 0.0f;
		gc->last_density = gc->default_density;
		if (gc->default_density > 0.001f) {
			gc->schedule_counter = gc->random_float() * (p_mix_rate / gc->default_density);
		} else {
			gc->schedule_counter = 1.0e9f;
		}

		DEV_ASSERT(p_arena.get_remaining() < p_arena.capacity);
		return gc;
	}
};
