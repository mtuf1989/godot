#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/config/project_settings.h"
#include "core/variant/dictionary.h"
#include "core/variant/array.h"
#include "scene/resources/curve.h"
#include <atomic>

class SoundEvent;

// Game-level voice pool manager. Fixed-size pool with state machine,
// priority-based stealing, virtualization, and lock-free metrics.
// This is the public API for the Game Audio Layer.
class SymphonyVoicePool : public Object {
	GDCLASS(SymphonyVoicePool, Object);

public:
	static constexpr int ANTI_CLICK_SAMPLES = 64;
	static constexpr int MAX_LOCAL_PARAMS = 8;
	static constexpr int EVENT_LOG_SIZE = 64;

	enum EventResult {
		EVENT_PLAYED = 0,
		EVENT_STOLEN,
		EVENT_REJECTED_COOLDOWN,
		EVENT_REJECTED_VOICE_LIMIT,
		EVENT_REJECTED_NO_STREAMS,
		EVENT_VIRTUALIZED,
		EVENT_DEVIRTUALIZED,
	};

	struct AudioEventLog {
		int64_t timestamp_usec = 0;
		StringName event_name;
		EventResult result = EVENT_PLAYED;
		int voice_slot = -1;
		float importance = 0.0f;
		StringName steal_reason; // e.g. "lowest_importance", "cooldown", "voice_limit"
	};

	enum VoiceState {
		VOICE_FREE = 0,
		VOICE_TO_PLAY,
		VOICE_PLAYING,
		VOICE_VIRTUALIZING,
		VOICE_VIRTUAL,
		VOICE_DEVIRTUALIZING,
		VOICE_STOPPING,
		VOICE_STOPPED,
	};

	struct VoiceSlot {
		VoiceState state = VOICE_FREE;
		uint64_t event_id = 0;
		int priority = 50;
		float importance = 0.0f;
		float rms = 0.0f;
		bool rms_valid = false;
		Vector3 position;
		float fade_progress = 0.0f;
		float fade_speed = 0.0f;
		uint64_t start_time = 0;
		int category = 0; // SoundEvent::Category (0=SFX,1=Music,2=UI,3=Ambient,4=Voice)
		float importance_weight = 1.0f; // Per-event weight from SoundEvent

		// Distance attenuation
		int spatial_mode = 0; // SoundEvent::SpatialMode (0=NonPositional,1=2D,2=3D)
		int attenuation_model = 0; // SoundEvent::AttenuationModel (0=Linear,1=Log,2=Custom)
		float max_distance = 2000.0f;
		float inner_radius = 0.0f;
		float falloff_distance = 0.0f;
		float attenuation_volume = 1.0f; // 0.0 (silent) to 1.0 (full volume) — computed each update
		bool virtualize_when_inaudible = true;

		// Propagation delay (Task 11): while > 0, the slot is held in VOICE_TO_PLAY
		// and counts down each frame before transitioning to VOICE_PLAYING. This
		// realizes a deferred one-shot start without a SceneTreeTimer or any
		// per-play allocation (the field lives in the pre-allocated slot).
		float pending_start_delay_s = 0.0f;

		// Per-voice local RTPC parameters (override global)
		StringName local_param_names[MAX_LOCAL_PARAMS];
		float local_param_values[MAX_LOCAL_PARAMS];
		int local_param_count = 0;

		// LOD state
		int current_lod = 0;      // Current LOD tier (0=full, 1=simplified, 2=minimal)
		int target_lod = 0;       // Target LOD (set by auto or force_lod)
		bool lod_forced = false;  // If true, auto-LOD is disabled for this slot
		float lod_threshold_1 = 0.3f; // Distance ratio for LOD 0→1 transition
		float lod_threshold_2 = 0.7f; // Distance ratio for LOD 1→2 transition
	};

	struct VoiceMetrics {
		int active = 0;
		int virtual_count = 0;
		int stolen_this_frame = 0;
		float budget_percent = 0.0f;
	};

private:
	static SymphonyVoicePool *singleton;

	VoiceSlot *slots = nullptr;
	int pool_size = 48;
	std::atomic<VoiceMetrics> metrics;
	int stolen_this_frame = 0;

	// Event log ring buffer (written on main thread, read on main thread — no thread safety issue)
	AudioEventLog event_log[EVENT_LOG_SIZE];
	int event_log_write_index = 0;
	int event_log_count = 0; // Total events written (saturates at EVENT_LOG_SIZE for read logic)

	void _log_event(const StringName &p_event_name, EventResult p_result, int p_slot, float p_importance, const StringName &p_steal_reason = StringName());

	// Per-slot attenuation curves (separate from VoiceSlot for cache reasons)
	Ref<Curve> *slot_attenuation_curves = nullptr;

	// Importance computation state
	Vector3 listener_position;
	float ref_distance = 500.0f; // Reference distance for distance_factor calculation
	int importance_update_frame = 0; // Frame counter for staggered updates
	static constexpr int IMPORTANCE_UPDATE_INTERVAL = 4; // Update every N frames
	static constexpr float CATEGORY_WEIGHTS[5] = {1.0f, 1.0f, 1.5f, 0.5f, 2.0f}; // SFX, Music, UI, Ambient, Voice

	void _update_importance_batch(int p_start, int p_count);

	// Internal frame delta tracking for time-based countdowns (propagation delay).
	uint64_t last_process_usec = 0;

protected:
	static void _bind_methods();

public:
	static SymphonyVoicePool *get_singleton() { return singleton; }

	int acquire_slot(int p_priority); // Free slots only — never steals (plan §10).
	void release_slot(int p_index, bool p_immediate = false);
	// Reclaim an occupied slot for a new play (caller updates event voice counts).
	void reclaim_slot(int p_index, int p_priority, const StringName &p_steal_reason = StringName());
	// Legacy helper: selects lowest-importance playing slot index, or -1. Does not free.
	int find_lowest_importance_slot() const;
	int steal_lowest_importance(); // Deprecated path: find + reclaim for tests/compat
	void virtualize(int p_index);
	void devirtualize(int p_index);

	void set_slot_rms(int p_index, float p_rms);
	[[nodiscard]] bool is_slot_rms_valid(int p_index) const;

	VoiceState get_slot_state(int p_index) const;
	int get_pool_size() const { return pool_size; }
	VoiceSlot *get_slot(int p_index);
	const VoiceSlot *get_slot(int p_index) const;

	int get_active_voice_count() const;
	int get_virtual_voice_count() const;
	int get_stolen_this_frame() const;
	float get_budget_percent() const;

	// Per-voice local parameters
	void set_local_parameter(int p_slot, const StringName &p_name, float p_value);
	float get_local_parameter(int p_slot, const StringName &p_name) const;
	bool has_local_parameter(int p_slot, const StringName &p_name) const;
	void clear_local_parameters(int p_slot);

	// Importance-based mixing
	void set_listener_position(const Vector3 &p_pos);
	Vector3 get_listener_position() const;
	void set_reference_distance(float p_dist);
	float get_reference_distance() const;
	void set_slot_category(int p_slot, int p_category);
	void set_slot_importance_weight(int p_slot, float p_weight);
	void set_slot_position(int p_slot, const Vector3 &p_pos);
	float get_slot_importance(int p_slot) const;
	void update_importance(); // Called each frame (internally handles staggering)
	void update_importance_all(); // Forces all slots to update immediately (test/debug only)

	// Distance attenuation
	void set_slot_spatial_mode(int p_slot, int p_mode);
	void set_slot_attenuation_model(int p_slot, int p_model);
	void set_slot_max_distance(int p_slot, float p_distance);
	void set_slot_inner_radius(int p_slot, float p_radius);
	void set_slot_falloff_distance(int p_slot, float p_distance);
	void set_slot_virtualize_when_inaudible(int p_slot, bool p_virtualize);
	float get_slot_attenuation_volume(int p_slot) const;
	void set_slot_attenuation_curve(int p_slot, const Ref<Curve> &p_curve);
	Ref<Curve> get_slot_attenuation_curve(int p_slot) const;

	// Propagation delay (Task 11) — deferred one-shot start.
	// Sets the remaining start delay (seconds); the slot stays in VOICE_TO_PLAY
	// until it elapses. Clamped to >= 0. Setting 0 starts on the next frame.
	void set_slot_start_delay(int p_slot, float p_delay_s);
	float get_slot_start_delay(int p_slot) const;
	// True while the slot is still counting down its propagation delay.
	bool is_slot_start_pending(int p_slot) const;
	// Advance all pending propagation-delay countdowns by p_delta seconds.
	// Called from process_frame() with the wall-clock delta; exposed for
	// deterministic delta control (and testing).
	void tick_deferred_starts(float p_delta);

	// Event Log API
	void log_event(const StringName &p_event_name, EventResult p_result, int p_slot, float p_importance, const StringName &p_steal_reason = StringName());
	Array get_recent_events(int p_count = 20) const;
	int get_event_log_count() const { return event_log_count; }

	// Per-category voice counts (for Performance monitors)
	Dictionary get_category_voice_counts() const;

	// LOD control
	void force_lod(int p_slot, int p_lod_tier);    // Force a specific LOD tier (disables auto)
	void release_lod_force(int p_slot);             // Re-enable auto-LOD for this slot
	int get_slot_current_lod(int p_slot) const;
	int get_slot_target_lod(int p_slot) const;
	void update_lod_targets();                      // Called each frame, updates target LOD based on distance
	void set_slot_lod_thresholds(int p_slot, float p_threshold_1, float p_threshold_2);

	void process_frame();

	SymphonyVoicePool();
	~SymphonyVoicePool();
};

VARIANT_ENUM_CAST(SymphonyVoicePool::VoiceState);
VARIANT_ENUM_CAST(SymphonyVoicePool::EventResult);
