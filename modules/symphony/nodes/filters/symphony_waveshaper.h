#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Table-based nonlinear transfer function (waveshaping distortion).
// Applies a pre-computed transfer curve via table lookup with linear interpolation.
// Drive amplifies the input signal before the lookup, controlling distortion intensity.
class SymphonyWaveshaper : public SymphonyOperator {
public:
	enum Preset {
		PRESET_SOFT_CLIP = 0, // tanh(x * 2)
		PRESET_HARD_CLIP = 1, // clamp(x * 2, -1, 1)
		PRESET_TUBE = 2,      // x / (1 + |x|) * 1.5
		PRESET_FOLDBACK = 3,  // sin(x * PI) for |x| > 1
		PRESET_CHEBYSHEV_3RD = 4, // 4x³ - 3x
		PRESET_CHEBYSHEV_5TH = 5, // 16x⁵ - 20x³ + 5x
		PRESET_MAX
	};

private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT drive_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	float *table = nullptr;      // Arena-allocated transfer curve
	int32_t table_size = 512;    // Number of entries in the table
	float default_drive = 1.0f;  // Drive multiplier when no input connected

public:
	SymphonyWaveshaper(float *p_table, int32_t p_table_size, float p_drive)
			: table(p_table), table_size(p_table_size), default_drive(p_drive) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		drive_input = (const float *)p_input_ptrs[1];
		audio_out = (float *)p_output_ptrs[0];
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float drive = drive_input ? *drive_input : default_drive;
		const float table_max = (float)(table_size - 1);

		SYMPHONY_UNROLL
		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = audio_in ? audio_in[i] : 0.0f;

			// Apply drive and clamp to [-1, 1]
			float driven = input * drive;
			driven = CLAMP(driven, -1.0f, 1.0f);

			// Map [-1, 1] to [0, table_size - 1]
			float pos = (driven + 1.0f) * 0.5f * table_max;

			// Linear interpolation between adjacent table entries
			int32_t idx = (int32_t)pos;
			if (idx < 0) idx = 0;
			if (idx >= table_size - 1) idx = table_size - 2;
			float frac = pos - (float)idx;

			audio_out[i] = table[idx] + frac * (table[idx + 1] - table[idx]);
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// Export table_size + table data for state migration
		size_t needed = sizeof(int32_t) + sizeof(float) * table_size;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		memcpy(p_buffer, &table_size, sizeof(int32_t));
		memcpy(p_buffer + sizeof(int32_t), table, sizeof(float) * table_size);
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t needed = sizeof(int32_t) + sizeof(float) * table_size;
		if (p_size < needed) return;
		int32_t imported_size = 0;
		memcpy(&imported_size, p_buffer, sizeof(int32_t));
		if (imported_size != table_size) return; // Size mismatch, skip
		memcpy(table, p_buffer + sizeof(int32_t), sizeof(float) * table_size);
	}

	// --- Preset table generation ---

	static void generate_table(float *p_table, int32_t p_size, Preset p_preset) {
		for (int32_t i = 0; i < p_size; i++) {
			// Map table index to [-1, 1]
			float x = ((float)i / (float)(p_size - 1)) * 2.0f - 1.0f;
			p_table[i] = evaluate_preset(x, p_preset);
		}
	}

	static float evaluate_preset(float x, Preset p_preset) {
		switch (p_preset) {
			case PRESET_SOFT_CLIP:
				return Math::tanh(x * 2.0f);

			case PRESET_HARD_CLIP:
				return CLAMP(x * 2.0f, -1.0f, 1.0f);

			case PRESET_TUBE: {
				// Asymmetric soft clip: x / (1 + |x|) * 1.5
				float abs_x = Math::abs(x);
				return (x / (1.0f + abs_x)) * 1.5f;
			}

			case PRESET_FOLDBACK: {
				// Fold back when |x| > 1 (after internal scaling)
				float scaled = x * 2.0f;
				if (Math::abs(scaled) > 1.0f) {
					return Math::sin(scaled * Math::PI);
				}
				return scaled;
			}

			case PRESET_CHEBYSHEV_3RD: {
				// T3(x) = 4x³ - 3x
				return 4.0f * x * x * x - 3.0f * x;
			}

			case PRESET_CHEBYSHEV_5TH: {
				// T5(x) = 16x⁵ - 20x³ + 5x
				float x2 = x * x;
				float x3 = x2 * x;
				float x5 = x3 * x2;
				return 16.0f * x5 - 20.0f * x3 + 5.0f * x;
			}

			default:
				return x; // Linear pass-through
		}
	}

	// --- Registration ---

	static void register_operator() {
		OperatorDescriptor desc;
		desc.type_name = "Waveshaper";
		desc.category = "Filters";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "drive", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "drive", 1.0f, 0.1f, 10.0f, 0.1f });
		desc.params.push_back({ "preset", 0.0f, 0.0f, 5.0f, 1.0f });
		desc.params.push_back({ "table_size", 512.0f, 64.0f, 2048.0f, 1.0f });
		desc.state_size = sizeof(SymphonyWaveshaper);
		desc.state_align = alignof(SymphonyWaveshaper);
		desc.create_fn = &SymphonyWaveshaper::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float drive = p_params.has("drive") ? (float)p_params["drive"] : 1.0f;
		int32_t preset = p_params.has("preset") ? (int)(float)p_params["preset"] : 0;
		int32_t tbl_size = p_params.has("table_size") ? (int)(float)p_params["table_size"] : 512;

		// Clamp table_size to valid range
		if (tbl_size < 64) tbl_size = 64;
		if (tbl_size > 2048) tbl_size = 2048;

		// Allocate table in arena
		float *tbl = (float *)p_arena.alloc(sizeof(float) * tbl_size, alignof(float));
		if (!tbl) return nullptr;

		// Check if custom table data is provided via params
		if (p_params.has("table_data")) {
			// table_data is expected as a PackedFloat32Array
			PackedFloat32Array table_data = p_params["table_data"];
			int32_t copy_size = MIN(tbl_size, table_data.size());
			const float *src = table_data.ptr();
			memcpy(tbl, src, sizeof(float) * copy_size);
			// Zero-fill remainder if table_data is smaller
			if (copy_size < tbl_size) {
				memset(tbl + copy_size, 0, sizeof(float) * (tbl_size - copy_size));
			}
		} else {
			// Generate preset table
			Preset p = (Preset)CLAMP(preset, 0, (int)PRESET_MAX - 1);
			generate_table(tbl, tbl_size, p);
		}

		// Allocate operator in arena
		void *mem = p_arena.alloc(sizeof(SymphonyWaveshaper), alignof(SymphonyWaveshaper));
		if (!mem) return nullptr;
		return new (mem) SymphonyWaveshaper(tbl, tbl_size, drive);
	}
};
