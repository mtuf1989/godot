#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

// Table-based nonlinear transfer function with 1st-order ADAA anti-aliasing.
// Two tables are stored: f(x) (transfer curve) and F(x) (its antiderivative).
// ADAA: y[n] = (F(x[n]) - F(x[n-1])) / (x[n] - x[n-1])
// When x[n] ≈ x[n-1], falls back to direct table lookup of f(x).
//
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

	float *table = nullptr;          // Arena-allocated transfer curve f(x)
	float *antideriv_table = nullptr; // Arena-allocated antiderivative F(x)
	int32_t table_size = 512;        // Number of entries in each table
	float default_drive = 1.0f;      // Drive multiplier when no input connected

	// ADAA state
	float prev_x = 0.0f;   // Previous input sample (after drive, clamped to [-1,1])
	float prev_F = 0.0f;   // Previous antiderivative lookup value

public:
	SymphonyWaveshaper(float *p_table, float *p_antideriv_table, int32_t p_table_size, float p_drive)
			: table(p_table), antideriv_table(p_antideriv_table), table_size(p_table_size), default_drive(p_drive) {}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		drive_input = (const float *)p_input_ptrs[1];
		audio_out = (float *)p_output_ptrs[0];
	}

	// Lookup with linear interpolation in a table mapped over [-1, 1].
	[[nodiscard]] inline float table_lookup(const float *p_tbl, float x) const {
		const float table_max = (float)(table_size - 1);
		float pos = (x + 1.0f) * 0.5f * table_max;
		int32_t idx = (int32_t)pos;
		if (idx < 0) idx = 0;
		if (idx >= table_size - 1) idx = table_size - 2;
		float frac = pos - (float)idx;
		return p_tbl[idx] + frac * (p_tbl[idx + 1] - p_tbl[idx]);
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float drive = drive_input ? *drive_input : default_drive;

		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = audio_in ? audio_in[i] : 0.0f;

			// Apply drive and clamp to [-1, 1]
			float x1 = CLAMP(input * drive, -1.0f, 1.0f);

			// Lookup antiderivative at current sample
			float F1 = table_lookup(antideriv_table, x1);

			float diff = x1 - prev_x;

			if (fabsf(diff) > 1e-7f) {
				// ADAA: finite difference of antiderivatives
				audio_out[i] = (F1 - prev_F) / diff;
			} else {
				// Fallback: direct transfer function lookup
				audio_out[i] = table_lookup(table, x1);
			}

			prev_x = x1;
			prev_F = F1;
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// Export ADAA state + table data for full state migration
		size_t needed = sizeof(float) * 2 + sizeof(int32_t) + sizeof(float) * table_size;
		if (!p_buffer) return needed;
		if (p_max_size < needed) return 0;
		size_t offset = 0;
		memcpy(p_buffer + offset, &prev_x, sizeof(float)); offset += sizeof(float);
		memcpy(p_buffer + offset, &prev_F, sizeof(float)); offset += sizeof(float);
		memcpy(p_buffer + offset, &table_size, sizeof(int32_t)); offset += sizeof(int32_t);
		memcpy(p_buffer + offset, table, sizeof(float) * table_size);
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t min_needed = sizeof(float) * 2 + sizeof(int32_t);
		if (p_size < min_needed) return;
		size_t offset = 0;
		memcpy(&prev_x, p_buffer + offset, sizeof(float)); offset += sizeof(float);
		memcpy(&prev_F, p_buffer + offset, sizeof(float)); offset += sizeof(float);
		int32_t imported_size = 0;
		memcpy(&imported_size, p_buffer + offset, sizeof(int32_t)); offset += sizeof(int32_t);
		if (imported_size != table_size) return; // Size mismatch, skip table import
		if (p_size < offset + sizeof(float) * table_size) return;
		memcpy(table, p_buffer + offset, sizeof(float) * table_size);
		// Regenerate antiderivative table from imported transfer table
		integrate_table(table, antideriv_table, table_size);
	}

	// --- Table generation ---

	static void generate_table(float *p_table, int32_t p_size, Preset p_preset) {
		for (int32_t i = 0; i < p_size; i++) {
			float x = ((float)i / (float)(p_size - 1)) * 2.0f - 1.0f;
			p_table[i] = evaluate_preset(x, p_preset);
		}
	}

	// Numerical integration of a transfer table to produce its antiderivative table.
	// Uses the trapezoidal rule for integration. The antiderivative is scaled to match
	// the table's [-1, 1] domain mapping.
	static void integrate_table(const float *p_table, float *p_antideriv, int32_t p_size) {
		// dx between adjacent table entries in the [-1, 1] domain
		float dx = 2.0f / (float)(p_size - 1);

		p_antideriv[0] = 0.0f;
		for (int32_t i = 1; i < p_size; i++) {
			// Trapezoidal rule: F(x_i) = F(x_{i-1}) + 0.5 * (f(x_{i-1}) + f(x_i)) * dx
			p_antideriv[i] = p_antideriv[i - 1] + 0.5f * (p_table[i - 1] + p_table[i]) * dx;
		}
	}

	[[nodiscard]] static float evaluate_preset(float x, Preset p_preset) {
		switch (p_preset) {
			case PRESET_SOFT_CLIP:
				return Math::tanh(x * 2.0f);

			case PRESET_HARD_CLIP:
				return CLAMP(x * 2.0f, -1.0f, 1.0f);

			case PRESET_TUBE: {
				float abs_x = Math::abs(x);
				return (x / (1.0f + abs_x)) * 1.5f;
			}

			case PRESET_FOLDBACK: {
				float scaled = x * 2.0f;
				if (Math::abs(scaled) > 1.0f) {
					return Math::sin(scaled * Math::PI);
				}
				return scaled;
			}

			case PRESET_CHEBYSHEV_3RD: {
				return 4.0f * x * x * x - 3.0f * x;
			}

			case PRESET_CHEBYSHEV_5TH: {
				float x2 = x * x;
				float x3 = x2 * x;
				float x5 = x3 * x2;
				return 16.0f * x5 - 20.0f * x3 + 5.0f * x;
			}

			default:
				return x;
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
		desc.nonlinear = true; // Flag for anti-alias staircase (P1b)
		desc.state_size = sizeof(SymphonyWaveshaper);
		desc.state_align = alignof(SymphonyWaveshaper);
		// Two tables: transfer + antiderivative, each up to 2048 floats + alignment
		desc.extra_arena_bytes = sizeof(float) * 2048 * 2 + 64;
		desc.create_fn = &SymphonyWaveshaper::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		float drive = p_params.has("drive") ? (float)p_params["drive"] : 1.0f;
		int32_t preset = p_params.has("preset") ? (int)(float)p_params["preset"] : 0;
		int32_t tbl_size = p_params.has("table_size") ? (int)(float)p_params["table_size"] : 512;

		if (tbl_size < 64) tbl_size = 64;
		if (tbl_size > 2048) tbl_size = 2048;

		// Allocate transfer table
		float *tbl = (float *)p_arena.alloc(sizeof(float) * tbl_size, alignof(float));
		if (!tbl) return nullptr;

		// Allocate antiderivative table
		float *antideriv = (float *)p_arena.alloc(sizeof(float) * tbl_size, alignof(float));
		if (!antideriv) return nullptr;

		// Fill transfer table
		if (p_params.has("table_data")) {
			PackedFloat32Array table_data = p_params["table_data"];
			int32_t copy_size = MIN(tbl_size, table_data.size());
			const float *src = table_data.ptr();
			memcpy(tbl, src, sizeof(float) * copy_size);
			if (copy_size < tbl_size) {
				memset(tbl + copy_size, 0, sizeof(float) * (tbl_size - copy_size));
			}
		} else {
			Preset p = (Preset)CLAMP(preset, 0, (int)PRESET_MAX - 1);
			generate_table(tbl, tbl_size, p);
		}

		// Compute antiderivative table from transfer table (works for both preset and custom)
		integrate_table(tbl, antideriv, tbl_size);

		// Allocate operator
		void *mem = p_arena.alloc(sizeof(SymphonyWaveshaper), alignof(SymphonyWaveshaper));
		if (!mem) return nullptr;
		return new (mem) SymphonyWaveshaper(tbl, antideriv, tbl_size, drive);
	}
};
