#ifndef REVERB_POOL_H
#define REVERB_POOL_H

#include "core/math/math_funcs.h"
#include "core/templates/hash_map.h"

// Shared reverb pool (Task 10 — Spatial Acoustics Core).
//
// Objective: expensive reverb, correctly shared, zero runtime bus churn (R4).
//
// This is the C++ *manager* side of the shared reverb pool. It owns no audio
// routing itself — Godot's audio buses (one persistent reverb send bus per
// pool slot) are created ONCE at init by the GDScript AudioManager (plan
// option (b)). This class decides, on the main thread:
//
//   • how many distinct reverb "voices" (slots) are active,
//   • which pool slot each emitter is assigned to (clustered by acoustic
//     similarity — RT60 proximity before rooms land in S6),
//   • the aggregated FDN reverb parameters per slot (decay_time from RT60,
//     damping from mean high-band absorption),
//   • the per-emitter send level, including a crossfade when an emitter
//     migrates from one slot to another (no click on room change).
//
// HARD REQUIREMENT (R4): this class NEVER calls AudioServer::add_bus_effect
// or remove_bus (or any AudioServer mutation). It is pure data. The
// `touched_audio_server()` accessor stays false for the lifetime of the pool
// and is asserted in tests as an explicit anti-regression guard — bus churn
// is the reference addon's defining flaw and must not reappear.
//
// GPU offload: SlotParams is a POD, tightly packed, and the whole active
// block is exposed as a contiguous array via slot_param_block() so a future
// GPU DSP backend can consume it without any refactor (GPU_DSP_BACKEND_v2.md).
class ReverbPool {
public:
	static constexpr int MAX_SLOTS = 8;

	// Per-slot FDN reverb parameter block. POD, contiguous, GPU-friendly.
	// Field layout mirrors SymphonyFDNReverb inputs so it can drive an FDN
	// instance directly (room_size, decay_time, damping, pre_delay_ms) plus a
	// wet gain used to fade a slot in/out as its cluster gains/loses members.
	struct SlotParams {
		float room_size = 0.5f;    // 0..1 normalized (derived from RT60)
		float decay_time = 2.0f;   // seconds (driven by RT60)
		float damping = 0.5f;      // 0..1 HF damping (mean high-band absorption)
		float pre_delay_ms = 20.0f; // fixed baseline pre-delay
		float wet_gain = 0.0f;     // 0..1 slot activity (0 = idle/free)
	};

	// Per-emitter assignment record, including migration crossfade state.
	struct EmitterAssignment {
		int slot = -1;          // Current (target) slot, -1 = unassigned
		int prev_slot = -1;     // Slot being crossfaded out of, -1 = none
		float crossfade = 1.0f; // 1 = fully on `slot`, <1 = fading from prev_slot
		float send = 0.0f;      // Base reverb send level [0,1] for this emitter
		// Cached acoustic descriptor for clustering / re-aggregation.
		float rt60 = 0.0f;
		float damping = 0.5f;
		bool active = false;
		float time_on_slot = 0.0f; // seconds since the last migration (Phase 4.3 dwell)
		float volume = 0.0f;       // estimated room volume m³ (Phase 4.4 room_size source)
	};

	struct Config {
		int active_slots = 8;              // N (8 desktop / 4 mobile / 2 web)
		float rt60_cluster_threshold = 0.4f; // seconds; emitters within this RT60 share a slot
		float crossfade_seconds = 0.25f;   // migration crossfade duration
		float max_rt60 = 10.0f;            // decay_time clamp ceiling
		// Phase 4.3 — anti-oscillation for a boundary emitter:
		float rt60_cluster_hysteresis = 0.15f; // a rival slot must beat the current by this (s) to win
		float min_dwell_seconds = 0.3f;    // minimum time on a slot before another migration is allowed
	};

	struct Metrics {
		int active_slot_count = 0;   // slots with >=1 assigned emitter
		int assigned_emitters = 0;   // total emitters currently assigned
		int migrations = 0;          // migrations started since last reset
		int degraded_assignments = 0; // assignments that fell back to nearest match (pool full)
	};

private:
	Config config;
	Metrics metrics;

	// Slot state.
	SlotParams slots[MAX_SLOTS];
	// Cluster centroid RT60 per slot (running mean of assigned emitters).
	float slot_centroid_rt60[MAX_SLOTS] = {};
	// Aggregation accumulators, recomputed each update from assigned emitters.
	float slot_sum_rt60[MAX_SLOTS] = {};
	float slot_sum_damping[MAX_SLOTS] = {};
	float slot_sum_volume[MAX_SLOTS] = {}; // Phase 4.4 — weighted volume per slot.
	int slot_member_count[MAX_SLOTS] = {};

	// Emitter assignments keyed by emitter id (voice slot).
	HashMap<int, EmitterAssignment> assignments;

	// Anti-regression: proves the pool never mutates AudioServer.
	bool audio_server_touched = false;

	float _crossfade_rate() const {
		return (config.crossfade_seconds > 0.0001f) ? (1.0f / config.crossfade_seconds) : 1e9f;
	}

	// Map RT60 (seconds) → FDN room_size (0..1). Larger rooms ring longer.
	static float _rt60_to_room_size(float p_rt60, float p_max_rt60);
	// Map estimated volume (m³) → FDN room_size (0..1). Returns 0 if no volume.
	static float _volume_to_room_size(float p_volume);

	// Find the best slot for a given RT60. Returns slot index and sets
	// r_degraded if the pool was full and we fell back to nearest match.
	int _find_slot_for(float p_rt60, bool &r_degraded);

public:
	void init(const Config &p_config);
	void reset();

	// Assign (or re-assign) an emitter. Called on the main thread when the
	// emitter's room acoustics are (re)solved. Returns the assigned slot.
	// Starts a crossfade if the assignment changed from a previous slot.
	// p_volume (m³, Phase 4.4) drives per-slot room_size; pass 0 to fall back to
	// the RT60-derived estimate.
	int assign(int p_emitter_id, float p_rt60, float p_damping, float p_reverb_send, float p_volume = 0.0f);

	// Remove an emitter from the pool (voice released).
	void release(int p_emitter_id);

	// Advance crossfades and recompute per-slot aggregated parameters.
	// Call once per frame after all assign() calls for the frame.
	void update(float p_delta);

	// --- Read accessors (audio thread / debug) ---
	const SlotParams &slot_params(int p_slot) const;
	// Contiguous block of the active slots (for GPU offload / batch upload).
	const SlotParams *slot_param_block() const { return slots; }
	int active_slot_count() const { return config.active_slots; }

	// Per-emitter resolved routing. Returns false if the emitter is unassigned.
	bool emitter_slot(int p_emitter_id, int &r_slot, float &r_send) const;
	// During a crossfade, the emitter also sends to prev_slot with (1-crossfade).
	bool emitter_prev_slot(int p_emitter_id, int &r_prev_slot, float &r_send) const;
	bool is_migrating(int p_emitter_id) const;

	// --- Anti-regression ---
	bool touched_audio_server() const { return audio_server_touched; }

	// --- Metrics ---
	const Metrics &get_metrics() const { return metrics; }
	void reset_migration_metrics() { metrics.migrations = 0; metrics.degraded_assignments = 0; }

	const Config &get_config() const { return config; }

	ReverbPool();
};

#endif // REVERB_POOL_H
