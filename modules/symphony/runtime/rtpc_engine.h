#pragma once

#include "core/object/object.h"
#include "core/object/class_db.h"
#include "core/math/math_funcs.h"
#include "core/templates/hash_map.h"
#include "core/string/string_name.h"
#include "scene/resources/curve.h"
#include <atomic>
#include <cstdint>

// Real-Time Parameter Control Engine.
// Fixed preallocated slot registry; main-thread registration returns stable handles.
// Audio accesses slots by handle only (plan §9).
class RTPCEngine : public Object {
	GDCLASS(RTPCEngine, Object);

public:
	using Handle = int32_t;
	static constexpr Handle INVALID_HANDLE = -1;

	static constexpr int MAX_GLOBAL_PARAMETERS = 128;
	static constexpr int MAX_LOCAL_PARAMS_PER_VOICE = 8;
	static constexpr int MAX_ANALYSIS_OUTPUTS = 64;
	static constexpr float DEFAULT_SMOOTH_TIME_MS = 5.0f;
	static constexpr size_t CACHE_LINE_SIZE = 64;

	struct alignas(CACHE_LINE_SIZE) RTPCParameter {
		std::atomic<float> current_value{ 0.0f };
		float block_alpha = 1.0f; // Precomputed for current mix frame count
		float smooth_time_ms = DEFAULT_SMOOTH_TIME_MS;
		bool active = false;
		char _pad[CACHE_LINE_SIZE - sizeof(std::atomic<float>) - sizeof(float) - sizeof(float) - sizeof(bool)];
		std::atomic<float> target_value{ 0.0f };
		StringName name;
	};

	struct alignas(CACHE_LINE_SIZE) AnalysisOutput {
		std::atomic<float> value{ 0.0f };
		StringName name;
		bool active = false;
	};

private:
	static RTPCEngine *singleton;

	RTPCParameter global_params[MAX_GLOBAL_PARAMETERS];
	int global_param_count = 0;
	HashMap<StringName, Handle> global_param_index; // Main-thread lookup only

	AnalysisOutput analysis_outputs[MAX_ANALYSIS_OUTPUTS];
	int analysis_output_count = 0;
	HashMap<StringName, Handle> analysis_output_index; // Main-thread lookup only

	float sample_rate = 44100.0f;
	float default_smooth_time_ms = DEFAULT_SMOOTH_TIME_MS;
	int cached_mix_frames = 0;
	bool mix_callback_registered = false;
	std::atomic<uint64_t> missing_handle_count{ 0 };

	static void _mix_callback(void *p_userdata);
	void _recompute_block_alphas(int p_num_frames);
	[[nodiscard]] float _compute_block_alpha(float p_smooth_time_ms, int p_num_frames) const;

protected:
	static void _bind_methods();

public:
	static RTPCEngine *get_singleton() { return singleton; }

	// --- Main-thread registration (returns stable handle) ---
	Handle register_parameter(const StringName &p_name, float p_default_value = 0.0f, float p_smooth_time_ms = DEFAULT_SMOOTH_TIME_MS);
	Handle register_analysis_output(const StringName &p_name);

	[[nodiscard]] Handle find_parameter(const StringName &p_name) const;
	[[nodiscard]] Handle find_analysis_output(const StringName &p_name) const;

	// --- Handle-based access (audio-safe) ---
	bool set_target(Handle p_handle, float p_value);
	[[nodiscard]] float get_current_value(Handle p_handle) const;
	[[nodiscard]] float get_target_value(Handle p_handle) const;
	bool set_analysis_value(Handle p_handle, float p_value);
	[[nodiscard]] float get_analysis_value(Handle p_handle) const;

	// --- Name-based access (main thread; no auto-create) ---
	bool set_target(const StringName &p_name, float p_value);
	[[nodiscard]] bool has_parameter(const StringName &p_name) const;
	[[nodiscard]] float get_current_value(const StringName &p_name) const;
	[[nodiscard]] float get_target_value(const StringName &p_name) const;
	bool set_analysis_value(const StringName &p_name, float p_value);
	[[nodiscard]] float get_analysis_value(const StringName &p_name) const;
	[[nodiscard]] bool has_analysis_output(const StringName &p_name) const;

	void smooth_all(int p_num_frames);

	[[nodiscard]] float evaluate_curve(const Ref<Curve> &p_curve, float p_input, float p_min_out, float p_max_out) const;

	int get_analysis_output_count() const { return analysis_output_count; }
	StringName get_analysis_output_name(int p_index) const;
	float get_analysis_output_value_by_index(int p_index) const;

	Handle register_analysis(const String &p_name);
	bool set_analysis(const String &p_name, float p_value);
	float get_analysis(const String &p_name) const;
	bool has_analysis(const String &p_name) const;
	String get_analysis_name_at(int p_index) const;
	float get_analysis_value_at(int p_index) const;

	void set_sample_rate(float p_rate);
	float get_sample_rate() const { return sample_rate; }
	void set_default_smooth_time(float p_ms);
	float get_default_smooth_time() const { return default_smooth_time_ms; }
	int get_parameter_count() const { return global_param_count; }
	[[nodiscard]] uint64_t get_missing_handle_count() const;

	bool set_parameter_target(const String &p_name, float p_value);
	float get_parameter_value(const String &p_name) const;
	Handle register_global_parameter(const String &p_name, float p_default, float p_smooth_ms);
	bool has_global_parameter(const String &p_name) const;

	RTPCEngine();
	~RTPCEngine();
};
