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
	float delay_s = 0.0f;           // Propagation delay in seconds
	Vector3 apparent_position;       // May differ from true position (portal redirect)
	float _pad[1] = { 0.0f };       // Pad to keep alignment clean
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

	void _solve_occlusion_for_emitter(int p_emitter_idx, PhysicsDirectSpaceState3D *p_space);

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

	void set_listener_body_rid(const RID &p_rid);
	void clear_listener_body_rid();

	void set_physics_space(const RID &p_space_rid) { physics_space_rid = p_space_rid; }

	// --- Emitter position (for occlusion source) ---
	void set_emitter_position(int p_voice_slot, const Vector3 &p_position);

	// --- Debug ---
	int get_active_emitter_count() const;
	int get_max_emitters() const { return MAX_EMITTERS; }

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
