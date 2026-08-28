#ifndef SPATIAL_ACOUSTICS_ENGINE_H
#define SPATIAL_ACOUSTICS_ENGINE_H

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/math/vector3.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "symphony_seqlock.h"
#include "occlusion_solver.h"
#include "spatial_graph_wrapper.h"
#include "probe_scheduler.h"
#include "probe_cache.h"
#include "room_estimator.h"
#include "reverb_pool.h"
#include "portal_graph.h"
#include "portal_router.h"

// Per-emitter spatial parameters — the output of the main-thread simulation,
// consumed by the audio thread (via SeqLock) to drive DSP.
// POD struct, trivially copyable, fits within SeqLock's 256-byte limit.
struct SpatialParams {
	float attenuation = 1.0f;        // 0=silent, 1=full (from VoicePool, mirrored for convenience)
	float occlusion = 0.0f;          // 0=unoccluded, 1=fully occluded
	float transmission[3] = { 1.0f, 1.0f, 1.0f }; // Low/Mid/High band transmission [0,1]
	float air_cutoff = 20000.0f;     // Air absorption LPF cutoff Hz
	float reverb_send = 0.0f;        // Reverb send level [0,1]
	float rt60 = 0.0f;              // Current RT60 estimate for reverb assignment
	float damping = 0.5f;           // Reverb HF damping [0,1] (from room high-band absorption)
	float delay_s = 0.0f;           // Propagation delay in seconds
	float portal_gain = 1.0f;        // Portal per-hop aperture/incidence gain [0,1] (Task 15).
	                                 // Separate from `attenuation` (which VoicePool owns) so the
	                                 // GDScript layer applies it as its own gain layer — no double count.
	Vector3 apparent_position;       // May differ from true position (portal redirect)
};

static_assert(sizeof(SpatialParams) <= 256, "SpatialParams must fit in SeqLock");

// Centralized spatial acoustics engine. Runs on the main thread, producing
// smoothed SpatialParams that the audio thread reads via SeqLock.
//
// Lifecycle: Created as singleton in register_types.cpp.
// Tick: AudioManager calls update(delta) each frame from _process().
class SpatialAcousticsEngine : public Object {
	GDCLASS(SpatialAcousticsEngine, Object);

public:
	static constexpr int MAX_EMITTERS = 256;
	static constexpr float DEFAULT_SMOOTH_ALPHA = 0.75f; // IIR smoothing coefficient (Resonance production value)

private:
	static SpatialAcousticsEngine *singleton;

	// Per-emitter state (struct-of-arrays for cache efficiency).
	struct EmitterState {
		bool active = false;
		bool first_update = true;      // Snap on first update (UE5 pattern)
		int voice_slot = -1;           // Associated VoicePool slot
		Vector3 source_position;       // World position of this emitter
		float last_update_time = 0.0f; // Time since last occlusion solve (seconds)
		float source_radius = 0.0f;    // Emitter radius (m) for volumetric occlusion (Task 12); 0 = point
		float max_distance = 0.0f;     // Attenuation max distance (m); used for air absorption normalization

		// Target values (set by solvers before smoothing)
		SpatialParams target;

		// Smoothed values (published via SeqLock)
		SpatialParams smoothed;

		// Published via SeqLock for audio thread consumption
		SymphonySeqLock<SpatialParams> published;
	};

	EmitterState emitters[MAX_EMITTERS];

	// Map: voice slot → emitter index for O(1) lookup
	HashMap<int, int> slot_to_emitter;

	// Next free emitter slot (simple linear scan; fine for ≤256)
	int _find_free_emitter() const;

	// Smoothing
	float smooth_alpha = DEFAULT_SMOOTH_ALPHA;

	void _smooth_params(int p_emitter_idx, float p_delta);
	void _publish_params(int p_emitter_idx);

	// Occlusion
	OcclusionSolver::Config occlusion_config;
	Vector<RID> exclude_rids; // Listener body RID(s) to exclude from occlusion rays
	bool occlusion_enabled = true;
	RID physics_space_rid; // Set by AudioManager from World3D.get_space()

	// Volumetric occlusion (Task 12)
	OcclusionSolver::VolumetricConfig volumetric_config;
	bool volumetric_occlusion_enabled = true;
	// Air absorption: distance beyond which HF rolloff reaches its floor.
	float air_absorption_max_distance = 100.0f;
	bool air_absorption_enabled = true;
	// Volume sample count for volumetric occlusion this frame (budget-scaled).
	int _volumetric_samples_this_frame = 8;

	void _solve_occlusion_for_emitter(int p_emitter_idx, PhysicsDirectSpaceState3D *p_space);

	// Room estimation
	RoomEstimator::Config room_config;
	bool room_estimation_enabled = true;
	void _solve_room_for_emitter(int p_emitter_idx, PhysicsDirectSpaceState3D *p_space);

	// Probe scheduler and cache
	ProbeScheduler scheduler;
	ProbeCache probe_cache;
	Vector<int> scheduled_emitters; // Scratch buffer for scheduler output

	// Shared reverb pool (Task 10) — clusters emitters by RT60 proximity and
	// assigns each cluster a pre-allocated pool slot. Never mutates AudioServer.
	ReverbPool reverb_pool;
	bool reverb_pool_enabled = true;
	void _update_reverb_pool(float p_delta);

	// Portal propagation (Task 15) — rooms=nodes, portals=weighted edges.
	// Rebuilt from the live AcousticRoom3D/AcousticPortal3D registries when the
	// room or portal epoch changes. Paths cached per room-pair, invalidated on
	// portal state change.
	PortalGraph portal_graph;
	DijkstraPathSolver portal_solver;
	PortalPathCache portal_path_cache;
	bool portal_propagation_enabled = true;
	uint64_t portal_graph_epoch = 0; // last (room_epoch ^ portal_epoch) the graph was built for
	float portal_diffraction_min_cutoff = 700.0f;
	// Rebuild portal_graph from registries if the epoch changed. Maps each
	// AcousticRoom3D* to its node index (in registry order).
	HashMap<uint64_t, int> _room_ptr_to_node; // ObjectID → node index
	void _rebuild_portal_graph_if_needed();
	// Resolve portal routing for one emitter and fold results into its target.
	void _solve_portals_for_emitter(int p_emitter_idx);

	// Listener position (updated from AudioManager)
	Vector3 listener_position;

protected:
	static void _bind_methods();

public:
	static SpatialAcousticsEngine *get_singleton() { return singleton; }

	// --- Emitter lifecycle ---
	// Register an emitter for spatial processing. Returns emitter handle (index) or -1.
	int register_emitter(int p_voice_slot);
	void unregister_emitter(int p_voice_slot);
	bool has_emitter(int p_voice_slot) const;

	// --- Per-frame update (called by AudioManager._process) ---
	void update(float p_delta);

	// --- Target setters (called by solvers: occlusion, room estimation, etc.) ---
	void set_emitter_occlusion(int p_voice_slot, float p_occlusion);
	void set_emitter_transmission(int p_voice_slot, float p_low, float p_mid, float p_high);
	void set_emitter_air_cutoff(int p_voice_slot, float p_cutoff_hz);
	void set_emitter_reverb_send(int p_voice_slot, float p_send);
	void set_emitter_rt60(int p_voice_slot, float p_rt60);
	void set_emitter_delay(int p_voice_slot, float p_delay_s);
	void set_emitter_apparent_position(int p_voice_slot, const Vector3 &p_position);

	// --- Reader access (audio thread or debug overlay) ---
	// Returns the most recently published params for a voice slot.
	SpatialParams read_params(int p_voice_slot) const;
	bool try_read_params(int p_voice_slot, SpatialParams &r_params) const;

	// --- Configuration ---
	void set_smooth_alpha(float p_alpha);
	float get_smooth_alpha() const { return smooth_alpha; }

	void set_listener_position(const Vector3 &p_pos) { listener_position = p_pos; }
	Vector3 get_listener_position() const { return listener_position; }

	// --- Occlusion configuration ---
	void set_occlusion_enabled(bool p_enabled) { occlusion_enabled = p_enabled; }
	bool get_occlusion_enabled() const { return occlusion_enabled; }
	void set_occlusion_max_hits(int p_max) { occlusion_config.max_hits = CLAMP(p_max, 1, 32); }
	int get_occlusion_max_hits() const { return occlusion_config.max_hits; }

	void set_occlusion_collision_mask(uint32_t p_mask) { occlusion_config.collision_mask = p_mask; }
	uint32_t get_occlusion_collision_mask() const { return occlusion_config.collision_mask; }

	// --- Room estimation configuration ---
	void set_room_estimation_enabled(bool p_enabled) { room_estimation_enabled = p_enabled; }
	bool get_room_estimation_enabled() const { return room_estimation_enabled; }
	void set_room_ray_count(int p_count) { room_config.ray_count = CLAMP(p_count, 4, 128); }
	int get_room_ray_count() const { return room_config.ray_count; }
	void set_room_max_distance(float p_dist) { room_config.max_distance = MAX(p_dist, 1.0f); }
	float get_room_max_distance() const { return room_config.max_distance; }
	void set_room_ignore_floor(bool p_ignore) { room_config.ignore_floor = p_ignore; }
	bool get_room_ignore_floor() const { return room_config.ignore_floor; }

	void set_listener_body_rid(const RID &p_rid);
	void clear_listener_body_rid();

	void set_physics_space(const RID &p_space_rid) { physics_space_rid = p_space_rid; }

	// --- Emitter position (for occlusion source) ---
	void set_emitter_position(int p_voice_slot, const Vector3 &p_position);

	// --- Emitter volumetric / air absorption inputs (Task 12) ---
	void set_emitter_source_radius(int p_voice_slot, float p_radius);
	float get_emitter_source_radius(int p_voice_slot) const;
	void set_emitter_max_distance(int p_voice_slot, float p_max_distance);
	float get_emitter_max_distance(int p_voice_slot) const;

	// --- Volumetric occlusion configuration (Task 12) ---
	void set_volumetric_occlusion_enabled(bool p_enabled) { volumetric_occlusion_enabled = p_enabled; }
	bool get_volumetric_occlusion_enabled() const { return volumetric_occlusion_enabled; }
	void set_volumetric_sample_count(int p_count) { volumetric_config.sample_count = CLAMP(p_count, 1, 128); }
	int get_volumetric_sample_count() const { return volumetric_config.sample_count; }

	// --- Air absorption configuration (Task 12) ---
	void set_air_absorption_enabled(bool p_enabled) { air_absorption_enabled = p_enabled; }
	bool get_air_absorption_enabled() const { return air_absorption_enabled; }
	void set_air_absorption_max_distance(float p_dist) { air_absorption_max_distance = MAX(p_dist, 1.0f); }
	float get_air_absorption_max_distance() const { return air_absorption_max_distance; }

	// --- Debug ---
	int get_active_emitter_count() const;
	int get_max_emitters() const { return MAX_EMITTERS; }

	// --- Scheduler configuration ---
	void set_ray_budget(int p_budget) { scheduler.set_ray_budget(p_budget); }
	int get_ray_budget() const { return scheduler.get_ray_budget(); }
	void set_scheduler_base_rate(float p_hz) { scheduler.set_base_rate_hz(p_hz); }
	float get_scheduler_base_rate() const { return scheduler.get_base_rate_hz(); }

	// --- Scheduler metrics (for Performance monitors) ---
	int get_scheduler_rays_issued() const { return scheduler.get_metrics().rays_issued; }
	int get_scheduler_emitters_serviced() const { return scheduler.get_metrics().emitters_serviced; }
	int get_scheduler_emitters_skipped() const { return scheduler.get_metrics().emitters_skipped; }

	// --- Probe cache ---
	void set_cache_cell_size(float p_size) { probe_cache.set_cell_size(p_size); }
	float get_cache_cell_size() const { return probe_cache.get_cell_size(); }
	int get_cache_hits() const { return probe_cache.get_metrics().hits; }
	int get_cache_misses() const { return probe_cache.get_metrics().misses; }
	void invalidate_cache() { probe_cache.invalidate_all(); }
	void invalidate_cache_near(const Vector3 &p_position, float p_radius) { probe_cache.invalidate_near(p_position, p_radius); }

	// --- Reverb pool (Task 10) ---
	// Configure the number of active pool slots (8 desktop / 4 mobile / 2 web).
	void set_reverb_pool_slots(int p_slots);
	int get_reverb_pool_slots() const { return reverb_pool.active_slot_count(); }
	void set_reverb_pool_enabled(bool p_enabled) { reverb_pool_enabled = p_enabled; }
	bool get_reverb_pool_enabled() const { return reverb_pool_enabled; }

	// Per-slot FDN parameters (for GDScript to drive persistent reverb buses).
	float get_reverb_slot_decay_time(int p_slot) const;
	float get_reverb_slot_damping(int p_slot) const;
	float get_reverb_slot_room_size(int p_slot) const;
	float get_reverb_slot_wet_gain(int p_slot) const;

	// Per-emitter resolved routing (which slot a voice sends to, and how much).
	int get_emitter_reverb_slot(int p_voice_slot) const;
	float get_emitter_reverb_send(int p_voice_slot) const;
	// During migration, the emitter also sends to a previous slot.
	int get_emitter_reverb_prev_slot(int p_voice_slot) const;
	float get_emitter_reverb_prev_send(int p_voice_slot) const;
	bool is_emitter_reverb_migrating(int p_voice_slot) const;

	// Reverb pool metrics (for the debug overlay / Performance tab).
	int get_reverb_active_slot_count() const { return reverb_pool.get_metrics().active_slot_count; }
	int get_reverb_assigned_emitters() const { return reverb_pool.get_metrics().assigned_emitters; }
	int get_reverb_migrations() const { return reverb_pool.get_metrics().migrations; }
	int get_reverb_degraded_assignments() const { return reverb_pool.get_metrics().degraded_assignments; }

	// Anti-regression guard (R4): true only if the pool ever mutated AudioServer.
	// Must remain false for the lifetime of the engine.
	bool reverb_pool_touched_audio_server() const { return reverb_pool.touched_audio_server(); }

	// --- Portal propagation (Task 15) ---
	void set_portal_propagation_enabled(bool p_enabled) { portal_propagation_enabled = p_enabled; }
	bool get_portal_propagation_enabled() const { return portal_propagation_enabled; }
	void set_portal_diffraction_min_cutoff(float p_hz) { portal_diffraction_min_cutoff = CLAMP(p_hz, 20.0f, 20000.0f); }
	float get_portal_diffraction_min_cutoff() const { return portal_diffraction_min_cutoff; }
	int get_portal_graph_room_count() const { return portal_graph.node_count; }
	int get_portal_graph_edge_count() const { return (int)portal_graph.edges.size(); }
	// Per-emitter portal routing outputs (smoothed), for the GDScript layer.
	Vector3 get_emitter_apparent_position(int p_voice_slot) const;
	float get_emitter_portal_gain(int p_voice_slot) const;

	// --- Graph wrapper (GDScript-accessible) ---
	// Wraps a plain AudioStream in a spatial-processing Symphony graph.
	Ref<AudioStreamSymphony> wrap_stream(const Ref<AudioStream> &p_stream, bool p_loop = false);
	// Returns true if the stream needs wrapping (is not already AudioStreamSymphony).
	bool stream_needs_wrapping(const Ref<AudioStream> &p_stream) const;
	// Compute filter cutoffs from spatial params (for per-frame parameter driving).
	float compute_occlusion_cutoff(int p_voice_slot) const;
	float compute_air_cutoff(int p_voice_slot) const;
	float compute_spatial_gain(int p_voice_slot) const;

	SpatialAcousticsEngine();
	~SpatialAcousticsEngine();
};

#endif // SPATIAL_ACOUSTICS_ENGINE_H
