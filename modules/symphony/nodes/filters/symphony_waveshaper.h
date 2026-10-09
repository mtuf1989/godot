#pragma once

#include "../../core/symphony_operator.h"
#include "../../core/symphony_operator_registry.h"
#include "../../core/symphony_arena_allocator.h"
#include "core/math/math_funcs.h"

#include <atomic>

// Table-based nonlinear transfer function with 1st-order ADAA anti-aliasing.
// Preset f/F tables are shared, immutable, and built once on the main thread
// during register. Custom table_data still lives in the arena.
//
// ADAA uses a three-way evaluation that respects linear table interpolation:
// same cell → midpoint f; adjacent cells → length-weighted midpoints;
// otherwise → difference of the quadratic antiderivative F.
class SymphonyWaveshaper : public SymphonyOperator {
public:
	enum Preset {
		PRESET_SOFT_CLIP = 0, // tanh(x * 2)
		PRESET_HARD_CLIP = 1, // clamp(x * 2, -1, 1)
		PRESET_TUBE = 2, // x / (1 + |x|) * 1.5
		PRESET_FOLDBACK = 3, // continuous triangle fold
		PRESET_CHEBYSHEV_3RD = 4, // 4x³ - 3x
		PRESET_CHEBYSHEV_5TH = 5, // 16x⁵ - 20x³ + 5x
		PRESET_MAX
	};

	static constexpr int32_t PRESET_TABLE_SIZE = 512;

private:
	const float *SYMPHONY_RESTRICT audio_in = nullptr;
	const float *SYMPHONY_RESTRICT drive_input = nullptr;
	float *SYMPHONY_RESTRICT audio_out = nullptr;

	const float *table = nullptr; // Transfer curve f(x); may be shared preset storage
	const float *antideriv_table = nullptr; // Antiderivative F(x)
	float *owned_table = nullptr; // Non-null when this instance owns arena tables
	float *owned_antideriv = nullptr;
	int32_t table_size = PRESET_TABLE_SIZE;
	float default_drive = 1.0f;

	float prev_x = 0.0f;
	float prev_F = 0.0f;

	// Shared preset tables (main-thread init during register).
	static float shared_f[PRESET_MAX][PRESET_TABLE_SIZE];
	static float shared_F[PRESET_MAX][PRESET_TABLE_SIZE];
	static std::atomic<bool> shared_ready;

public:
	SymphonyWaveshaper(const float *p_table, const float *p_antideriv_table, int32_t p_table_size, float p_drive,
			float *p_owned_table = nullptr, float *p_owned_antideriv = nullptr)
			: table(p_table), antideriv_table(p_antideriv_table), owned_table(p_owned_table),
			  owned_antideriv(p_owned_antideriv), table_size(p_table_size), default_drive(p_drive) {
		prev_F = eval_F(prev_x);
	}

	virtual void bind_pins(void **p_input_ptrs, void **p_output_ptrs) override {
		audio_in = (const float *)p_input_ptrs[0];
		drive_input = (const float *)p_input_ptrs[1];
		audio_out = (float *)p_output_ptrs[0];
	}

	[[nodiscard]] inline float table_lookup(const float *p_tbl, float p_x) const {
		const float table_max = (float)(table_size - 1);
		float pos = (p_x + 1.0f) * 0.5f * table_max;
		int32_t idx = (int32_t)pos;
		if (idx < 0) {
			idx = 0;
		}
		if (idx >= table_size - 1) {
			idx = table_size - 2;
		}
		float frac = pos - (float)idx;
		return p_tbl[idx] + frac * (p_tbl[idx + 1] - p_tbl[idx]);
	}

	[[nodiscard]] inline int32_t cell_index(float p_x) const {
		const float table_max = (float)(table_size - 1);
		float pos = (p_x + 1.0f) * 0.5f * table_max;
		int32_t idx = (int32_t)pos;
		if (idx < 0) {
			idx = 0;
		}
		if (idx >= table_size - 1) {
			idx = table_size - 2;
		}
		return idx;
	}

	[[nodiscard]] inline float knot_x(int32_t p_idx) const {
		return ((float)p_idx / (float)(table_size - 1)) * 2.0f - 1.0f;
	}

	// Quadratic antiderivative of the linearly interpolated f on cell i.
	[[nodiscard]] inline float eval_F_at(float p_x) const {
		const float dx = 2.0f / (float)(table_size - 1);
		int32_t i = cell_index(p_x);
		float x_i = knot_x(i);
		float t = (p_x - x_i) / dx;
		if (t < 0.0f) {
			t = 0.0f;
		}
		if (t > 1.0f) {
			t = 1.0f;
		}
		float df = table[i + 1] - table[i];
		return antideriv_table[i] + table[i] * t * dx + 0.5f * df * t * t * dx;
	}

	[[nodiscard]] inline float eval_F(float p_x) const {
		return eval_F_at(p_x);
	}

	[[nodiscard]] inline float eval_f(float p_x) const {
		return table_lookup(table, p_x);
	}

	inline float process_adaa(float p_x0, float p_x1) const {
		int32_t c0 = cell_index(p_x0);
		int32_t c1 = cell_index(p_x1);

		if (c0 == c1) {
			return eval_f(0.5f * (p_x0 + p_x1));
		}

		if (Math::abs(c1 - c0) == 1) {
			int32_t lo = MIN(c0, c1);
			float boundary = knot_x(lo + 1);
			float mid_a = 0.5f * (p_x0 + boundary);
			float mid_b = 0.5f * (boundary + p_x1);
			float w_a = fabsf(boundary - p_x0);
			float w_b = fabsf(p_x1 - boundary);
			float denom = w_a + w_b;
			if (denom < 1e-12f) {
				return eval_f(0.5f * (p_x0 + p_x1));
			}
			return (w_a * eval_f(mid_a) + w_b * eval_f(mid_b)) / denom;
		}

		float diff = p_x1 - p_x0;
		if (fabsf(diff) < 1e-12f) {
			return eval_f(0.5f * (p_x0 + p_x1));
		}
		return (eval_F_at(p_x1) - eval_F_at(p_x0)) / diff;
	}

	virtual void execute(int32_t p_num_frames) override {
		SYMPHONY_ASSUME_FRAMES(p_num_frames);

		float drive = drive_input ? *drive_input : default_drive;

		for (int32_t i = 0; i < p_num_frames; i++) {
			float input = audio_in ? audio_in[i] : 0.0f;
			float x1 = CLAMP(input * drive, -1.0f, 1.0f);
			audio_out[i] = process_adaa(prev_x, x1);
			prev_x = x1;
			prev_F = eval_F(x1);
		}
	}

	virtual size_t export_state(uint8_t *p_buffer, size_t p_max_size) const override {
		// Shared preset tables are immutable — only ADAA state migrates.
		// Custom tables also migrate the transfer curve (antideriv is rebuilt).
		size_t needed = sizeof(float) * 2 + sizeof(int32_t);
		if (owned_table) {
			needed += sizeof(float) * table_size;
		}
		if (!p_buffer) {
			return needed;
		}
		if (p_max_size < needed) {
			return 0;
		}
		size_t offset = 0;
		memcpy(p_buffer + offset, &prev_x, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &prev_F, sizeof(float));
		offset += sizeof(float);
		memcpy(p_buffer + offset, &table_size, sizeof(int32_t));
		offset += sizeof(int32_t);
		if (owned_table) {
			memcpy(p_buffer + offset, owned_table, sizeof(float) * table_size);
		}
		return needed;
	}

	virtual void import_state(const uint8_t *p_buffer, size_t p_size) override {
		size_t min_needed = sizeof(float) * 2 + sizeof(int32_t);
		if (p_size < min_needed) {
			return;
		}
		size_t offset = 0;
		memcpy(&prev_x, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		memcpy(&prev_F, p_buffer + offset, sizeof(float));
		offset += sizeof(float);
		int32_t imported_size = 0;
		memcpy(&imported_size, p_buffer + offset, sizeof(int32_t));
		offset += sizeof(int32_t);
		if (owned_table && imported_size == table_size && p_size >= offset + sizeof(float) * table_size) {
			memcpy(owned_table, p_buffer + offset, sizeof(float) * table_size);
			integrate_table(owned_table, owned_antideriv, table_size);
		}
		prev_F = eval_F(prev_x);
	}

	static void generate_table(float *p_table, int32_t p_size, Preset p_preset) {
		for (int32_t i = 0; i < p_size; i++) {
			float x = ((float)i / (float)(p_size - 1)) * 2.0f - 1.0f;
			p_table[i] = evaluate_preset(x, p_preset);
		}
	}

	static void integrate_table(const float *p_table, float *p_antideriv, int32_t p_size) {
		float dx = 2.0f / (float)(p_size - 1);
		p_antideriv[0] = 0.0f;
		for (int32_t i = 1; i < p_size; i++) {
			p_antideriv[i] = p_antideriv[i - 1] + 0.5f * (p_table[i - 1] + p_table[i]) * dx;
		}
	}

	// Continuous triangle fold: 4|frac(x/4 + 0.25) − 0.5| − 1, applied to 2x.
	[[nodiscard]] static float triangle_fold(float p_x) {
		float t = p_x * 0.25f + 0.25f;
		t -= floorf(t);
		return 4.0f * fabsf(t - 0.5f) - 1.0f;
	}

	[[nodiscard]] static float evaluate_preset(float p_x, Preset p_preset) {
		switch (p_preset) {
			case PRESET_SOFT_CLIP:
				return Math::tanh(p_x * 2.0f);

			case PRESET_HARD_CLIP:
				return CLAMP(p_x * 2.0f, -1.0f, 1.0f);

			case PRESET_TUBE: {
				float abs_x = Math::abs(p_x);
				return (p_x / (1.0f + abs_x)) * 1.5f;
			}

			case PRESET_FOLDBACK:
				return triangle_fold(p_x * 2.0f);

			case PRESET_CHEBYSHEV_3RD:
				return 4.0f * p_x * p_x * p_x - 3.0f * p_x;

			case PRESET_CHEBYSHEV_5TH: {
				float x2 = p_x * p_x;
				float x3 = x2 * p_x;
				float x5 = x3 * x2;
				return 16.0f * x5 - 20.0f * x3 + 5.0f * p_x;
			}

			default:
				return p_x;
		}
	}

	static void ensure_shared_tables() {
		if (shared_ready.load(std::memory_order_acquire)) {
			return;
		}
		for (int32_t p = 0; p < (int32_t)PRESET_MAX; p++) {
			generate_table(shared_f[p], PRESET_TABLE_SIZE, (Preset)p);
			integrate_table(shared_f[p], shared_F[p], PRESET_TABLE_SIZE);
		}
		shared_ready.store(true, std::memory_order_release);
	}

	static size_t extra_arena_bytes_for(const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		if (p_params.has("table_data")) {
			int32_t tbl_size = p_params.has("table_size") ? (int)(float)p_params["table_size"] : PRESET_TABLE_SIZE;
			if (tbl_size < 64) {
				tbl_size = 64;
			}
			if (tbl_size > 2048) {
				tbl_size = 2048;
			}
			return sizeof(float) * (size_t)tbl_size * 2 + 64;
		}
		return 0;
	}

	static void register_operator() {
		ensure_shared_tables();

		OperatorDescriptor desc;
		desc.type_name = "Waveshaper";
		desc.category = "Filters";
		desc.inputs.push_back({ "audio_in", SymphonyPinType::AUDIO, true });
		desc.inputs.push_back({ "drive", SymphonyPinType::FLOAT, false });
		desc.outputs.push_back({ "audio_out", SymphonyPinType::AUDIO, false });
		desc.params.push_back({ "drive", 1.0f, 0.1f, 10.0f, 0.1f });
		desc.params.push_back({ "preset", 0.0f, 0.0f, 5.0f, 1.0f });
		desc.params.push_back({ "table_size", 512.0f, 64.0f, 2048.0f, 1.0f });
		desc.nonlinear = true;
		desc.state_size = sizeof(SymphonyWaveshaper);
		desc.state_align = alignof(SymphonyWaveshaper);
		desc.extra_arena_bytes = 0;
		desc.extra_arena_bytes_fn = &SymphonyWaveshaper::extra_arena_bytes_for;
		desc.create_fn = &SymphonyWaveshaper::create;
		OperatorRegistry::get_singleton()->register_operator(desc);
	}

	static SymphonyOperator *create(ArenaAllocator &p_arena, const HashMap<StringName, Variant> &p_params, float p_mix_rate) {
		(void)p_mix_rate;
		ensure_shared_tables();

		float drive = p_params.has("drive") ? (float)p_params["drive"] : 1.0f;
		int32_t preset = p_params.has("preset") ? (int)(float)p_params["preset"] : 0;

		if (p_params.has("table_data")) {
			int32_t tbl_size = p_params.has("table_size") ? (int)(float)p_params["table_size"] : PRESET_TABLE_SIZE;
			if (tbl_size < 64) {
				tbl_size = 64;
			}
			if (tbl_size > 2048) {
				tbl_size = 2048;
			}

			float *tbl = (float *)p_arena.alloc(sizeof(float) * tbl_size, alignof(float));
			if (!tbl) {
				return nullptr;
			}
			float *antideriv = (float *)p_arena.alloc(sizeof(float) * tbl_size, alignof(float));
			if (!antideriv) {
				return nullptr;
			}

			PackedFloat32Array table_data = p_params["table_data"];
			int32_t copy_size = MIN(tbl_size, table_data.size());
			const float *src = table_data.ptr();
			memcpy(tbl, src, sizeof(float) * copy_size);
			if (copy_size < tbl_size) {
				memset(tbl + copy_size, 0, sizeof(float) * (tbl_size - copy_size));
			}
			integrate_table(tbl, antideriv, tbl_size);

			void *mem = p_arena.alloc(sizeof(SymphonyWaveshaper), alignof(SymphonyWaveshaper));
			if (!mem) {
				return nullptr;
			}
			return new (mem) SymphonyWaveshaper(tbl, antideriv, tbl_size, drive, tbl, antideriv);
		}

		Preset p = (Preset)CLAMP(preset, 0, (int)PRESET_MAX - 1);
		void *mem = p_arena.alloc(sizeof(SymphonyWaveshaper), alignof(SymphonyWaveshaper));
		if (!mem) {
			return nullptr;
		}
		return new (mem) SymphonyWaveshaper(shared_f[p], shared_F[p], PRESET_TABLE_SIZE, drive);
	}
};

inline float SymphonyWaveshaper::shared_f[SymphonyWaveshaper::PRESET_MAX][SymphonyWaveshaper::PRESET_TABLE_SIZE] = {};
inline float SymphonyWaveshaper::shared_F[SymphonyWaveshaper::PRESET_MAX][SymphonyWaveshaper::PRESET_TABLE_SIZE] = {};
inline std::atomic<bool> SymphonyWaveshaper::shared_ready{ false };
