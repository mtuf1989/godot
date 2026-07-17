#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/math/math_funcs.h"
#include "scene/resources/curve.h"
#include <atomic>
#include <new> // std::hardware_destructive_interference_size

// Real-Time Parameter Control Engine.
// Owns global parameters, smooths them on the audio thread, evaluates mapping curves.
// Per-voice local parameters are stored directly in VoiceSlot (fixed array).
class RTPCEngine : public Object {
	GDCLASS(RTPCEngine, Object);

public:
	static constexpr int MAX_GLOBAL_PARAMETERS = 128;
	static constexpr int MAX_LOCAL_PARAMS_PER_VOICE = 8;
	static constexpr float DEFAULT_SMOOTH_TIME_MS = 5.0f;

	// Cache-line size for false-sharing prevention.
	// std::hardware_destructive_interference_size is not available on all compilers/platforms,
	// so we use a conservative 64 bytes (correct for x86-64 and Apple Silicon).
	static constexpr size_t CACHE_LINE_SIZE = 64;

	// Layout: separate cache lines for audio-thread-owned and game-thread-owned data.
	// Without this, game thread writing target_value would invalidate the audio thread's
	// cache line containing current_value (false sharing), causing unnecessary cross-core
	// traffic on every set_target() call during smooth_all() iteration.
	struct alignas(CACHE_LINE_SIZE) RTPCParameter {
		// --- Audio-thread-owned (read/written every mix callback) ---
		std::atomic<float> current_value{0.0f};
		float smooth_coeff = 0.0f;
		bool active = false;

		// --- Padding to push target_value onto a separate cache line ---
		// current_value(4) + smooth_coeff(4) + active(1) = 9 bytes used.
		// Pad to 64 bytes so target_value starts on the next cache line.
		char _pad[CACHE_LINE_SIZE - sizeof(std::atomic<float>) - sizeof(float) - sizeof(bool)];

		// --- Game-thread-owned (written by set_target(), read by audio thread) ---
		std::atomic<float> target_value{0.0f};

		// --- Cold data (accessed infrequently: registration, debugging) ---
		StringName name;
	};

private:
	static RTPCEngine *singleton;

	// Global parameter registry — fixed-size array, audio-thread-safe.
	RTPCParameter global_params[MAX_GLOBAL_PARAMETERS];
	int global_param_count = 0;

	// Lookup: name → index into global_params
	HashMap<StringName, int> global_param_index;

	// Sample rate for coefficient calculation
	float sample_rate = 44100.0f;
	float default_smooth_time_ms = DEFAULT_SMOOTH_TIME_MS;

	// Mix callback for audio-thread smoothing
	static void _mix_callback(void *p_userdata);

	// Compute one-pole coefficient from time in ms
	float _compute_coeff(float p_smooth_time_ms) const;

protected:
	static void _bind_methods();

public:
	static RTPCEngine *get_singleton() { return singleton; }

	// --- Game thread API ---

	// Set a global parameter's target value (thread-safe: writes target atomically).
	void set_target(const StringName &p_name, float p_value);

	// Register a global parameter (call during setup, not audio thread).
	void register_parameter(const StringName &p_name, float p_default_value = 0.0f, float p_smooth_time_ms = DEFAULT_SMOOTH_TIME_MS);

	// Check if a parameter exists.
	[[nodiscard]] bool has_parameter(const StringName &p_name) const;

	// Get current smoothed value (may lag target by smoothing time).
	[[nodiscard]] float get_current_value(const StringName &p_name) const;

	// Get target value.
	[[nodiscard]] float get_target_value(const StringName &p_name) const;

	// --- Audio thread API (called from mix callback) ---

	// Advance all global parameters toward their targets.
	// Called once per mix cycle with the number of frames in this buffer.
	void smooth_all(int p_num_frames);

	// Evaluate a Godot Curve resource: maps input [0,1] → output [min_out, max_out].
	[[nodiscard]] float evaluate_curve(const Ref<Curve> &p_curve, float p_input, float p_min_out, float p_max_out) const;

	// --- Configuration ---

	void set_sample_rate(float p_rate);
	float get_sample_rate() const { return sample_rate; }

	void set_default_smooth_time(float p_ms);
	float get_default_smooth_time() const { return default_smooth_time_ms; }

	// Get parameter count for debugging
	int get_parameter_count() const { return global_param_count; }

	// GDScript-friendly versions
	void set_parameter_target(const String &p_name, float p_value);
	float get_parameter_value(const String &p_name) const;
	void register_global_parameter(const String &p_name, float p_default, float p_smooth_ms);
	bool has_global_parameter(const String &p_name) const;

	RTPCEngine();
	~RTPCEngine();
};
