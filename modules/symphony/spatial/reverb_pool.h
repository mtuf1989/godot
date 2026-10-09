#ifndef REVERB_POOL_H
#define REVERB_POOL_H

#include "core/math/math_funcs.h"
#include "core/math/vector3.h"
#include "core/templates/hash_map.h"
#include "reverb_level.h"

// Shared reverb pool — clusters emitters, exposes SlotParams for AudioEffectSymphonyReverb.
// HARD REQUIREMENT (R4): never mutates AudioServer.
class ReverbPool {
public:
	static constexpr int MAX_SLOTS = 8;

	// Per-slot late-reverb + ER parameter block. POD, contiguous, GPU-friendly.
	struct SlotParams {
		float decay_time = 2.0f; // RT60 seconds
		float absorption_high = 0.5f; // high-band absorption α_high
		float hf_ratio = 0.5f; // clamp(α_mean/α_high, 0.1, 1)
		float volume = 0.0f; // m³
		float dim_x = 0.0f;
		float dim_y = 0.0f;
		float dim_z = 0.0f;
		float er_reflection = 0.7f; // √(1 − α_mean)
		float critical_distance = 1.0f; // metres
	};

	struct EmitterAssignment {
		int slot = -1;
		int prev_slot = -1;
		float crossfade = 1.0f;
		float send = 0.0f;
		float rt60 = 0.0f;
		float absorption_high = 0.5f;
		bool active = false;
		float time_on_slot = 0.0f;
		float volume = 0.0f;
		Vector3 dims; // authored shoebox; zero = unauthored
	};

	struct Config {
		int active_slots = 8;
		float rt60_cluster_threshold = 0.4f;
		float crossfade_seconds = 0.25f;
		float max_rt60 = 10.0f;
		float rt60_cluster_hysteresis = 0.15f;
		float min_dwell_seconds = 0.3f;
		float drr_bias_db = 0.0f;
	};

	struct Metrics {
		int active_slot_count = 0;
		int assigned_emitters = 0;
		int migrations = 0;
		int degraded_assignments = 0;
	};

private:
	Config config;
	Metrics metrics;

	SlotParams slots[MAX_SLOTS];
	float slot_centroid_rt60[MAX_SLOTS] = {};
	float slot_sum_rt60[MAX_SLOTS] = {};
	float slot_sum_abs_high[MAX_SLOTS] = {};
	float slot_sum_volume[MAX_SLOTS] = {};
	float slot_sum_dim_x[MAX_SLOTS] = {};
	float slot_sum_dim_y[MAX_SLOTS] = {};
	float slot_sum_dim_z[MAX_SLOTS] = {};
	float slot_sum_authored[MAX_SLOTS] = {}; // weight of emitters with authored dims
	int slot_member_count[MAX_SLOTS] = {};

	HashMap<int, EmitterAssignment> assignments;
	bool audio_server_touched = false;

	float _crossfade_rate() const {
		return (config.crossfade_seconds > 0.0001f) ? (1.0f / config.crossfade_seconds) : 1e9f;
	}

	static Vector3 _aspect_dims_from_volume(float p_volume);
	static void _derive_slot_acoustics(float p_volume, float p_rt60, float p_abs_high, const Vector3 &p_dims,
			float p_drr_bias_db, SlotParams &r_out);

	int _find_slot_for(float p_rt60, bool &r_degraded);

public:
	void init(const Config &p_config);
	void reset();

	// p_dims: authored shoebox (W,H,D m); zero components → unauthored for that emitter.
	int assign(int p_emitter_id, float p_rt60, float p_absorption_high, float p_reverb_send,
			float p_volume = 0.0f, const Vector3 &p_dims = Vector3());

	void release(int p_emitter_id);
	void update(float p_delta);

	const SlotParams &slot_params(int p_slot) const;
	const SlotParams *slot_param_block() const { return slots; }
	int active_slot_count() const { return config.active_slots; }

	bool emitter_slot(int p_emitter_id, int &r_slot, float &r_send) const;
	bool emitter_prev_slot(int p_emitter_id, int &r_prev_slot, float &r_send) const;
	bool is_migrating(int p_emitter_id) const;
	bool emitter_descriptor(int p_emitter_id, float &r_rt60, float &r_volume, Vector3 &r_dims) const;

	bool touched_audio_server() const { return audio_server_touched; }

	const Metrics &get_metrics() const { return metrics; }
	void reset_migration_metrics() { metrics.migrations = 0; metrics.degraded_assignments = 0; }

	const Config &get_config() const { return config; }

	ReverbPool();
};

#endif // REVERB_POOL_H
