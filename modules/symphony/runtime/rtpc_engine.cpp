#include "rtpc_engine.h"
#include "core/object/class_db.h"
#include "servers/audio/audio_server.h"

#include <cmath>

RTPCEngine *RTPCEngine::singleton = nullptr;

RTPCEngine::RTPCEngine() {
	singleton = this;
	sample_rate = AudioServer::get_singleton()->get_mix_rate();
	AudioServer::get_singleton()->add_mix_callback(_mix_callback, this);
}

RTPCEngine::~RTPCEngine() {
	AudioServer::get_singleton()->remove_mix_callback(_mix_callback, this);
	singleton = nullptr;
}

void RTPCEngine::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parameter_target", "name", "value"), &RTPCEngine::set_parameter_target);
	ClassDB::bind_method(D_METHOD("get_parameter_value", "name"), &RTPCEngine::get_parameter_value);
	ClassDB::bind_method(D_METHOD("register_global_parameter", "name", "default_value", "smooth_ms"), &RTPCEngine::register_global_parameter, DEFVAL(0.0f), DEFVAL(DEFAULT_SMOOTH_TIME_MS));
	ClassDB::bind_method(D_METHOD("has_global_parameter", "name"), &RTPCEngine::has_global_parameter);
	ClassDB::bind_method(D_METHOD("get_parameter_count"), &RTPCEngine::get_parameter_count);
	ClassDB::bind_method(D_METHOD("set_default_smooth_time", "ms"), &RTPCEngine::set_default_smooth_time);
	ClassDB::bind_method(D_METHOD("get_default_smooth_time"), &RTPCEngine::get_default_smooth_time);
	ClassDB::bind_method(D_METHOD("evaluate_curve", "curve", "input", "min_out", "max_out"), &RTPCEngine::evaluate_curve);
}

// --- Mix callback (audio thread) ---

void RTPCEngine::_mix_callback(void *p_userdata) {
	RTPCEngine *engine = static_cast<RTPCEngine *>(p_userdata);
	// Use the internal mix buffer size as the frame count for this cycle.
	int frames = AudioServer::get_singleton()->thread_get_mix_buffer_size();
	engine->smooth_all(frames);
}

void RTPCEngine::smooth_all(int p_num_frames) {
	// Advance all active global parameters toward their targets.
	// Uses one-pole smoothing: current += coeff * (target - current)
	// The coeff is pre-computed per parameter based on smooth_time_ms.
	// We advance once per mix cycle (not per sample) since this is Float-rate.
	for (int i = 0; i < global_param_count; i++) {
		RTPCParameter &param = global_params[i];
		if (!param.active) {
			continue;
		}
		float target = param.target_value.load(std::memory_order_relaxed);
		float current = param.current_value.load(std::memory_order_relaxed);
		float diff = target - current;
		if (Math::abs(diff) < 1e-7f) {
			param.current_value.store(target, std::memory_order_relaxed);
		} else {
			// Apply smoothing: advance by coeff per micro-block worth of samples
			// coeff is computed for one micro-block advance
			float alpha = 1.0f - powf(1.0f - param.smooth_coeff, (float)p_num_frames);
			param.current_value.store(current + alpha * diff, std::memory_order_relaxed);
		}
	}
}

// --- Game thread API ---

void RTPCEngine::register_parameter(const StringName &p_name, float p_default_value, float p_smooth_time_ms) {
	ERR_FAIL_COND_MSG(global_param_count >= MAX_GLOBAL_PARAMETERS,
			"RTPCEngine: Maximum global parameter count reached.");
	ERR_FAIL_COND_MSG(global_param_index.has(p_name),
			"RTPCEngine: Parameter '" + String(p_name) + "' already registered.");

	int idx = global_param_count++;
	global_params[idx].name = p_name;
	global_params[idx].current_value.store(p_default_value, std::memory_order_relaxed);
	global_params[idx].target_value.store(p_default_value, std::memory_order_relaxed);
	global_params[idx].smooth_coeff = _compute_coeff(p_smooth_time_ms);
	global_params[idx].active = true;
	global_param_index[p_name] = idx;
}

void RTPCEngine::set_target(const StringName &p_name, float p_value) {
	int *idx_ptr = global_param_index.getptr(p_name);
	if (idx_ptr) {
		global_params[*idx_ptr].target_value.store(p_value, std::memory_order_relaxed);
	} else {
		// Auto-register on first use with default smoothing
		register_parameter(p_name, p_value, default_smooth_time_ms);
	}
}

bool RTPCEngine::has_parameter(const StringName &p_name) const {
	return global_param_index.has(p_name);
}

float RTPCEngine::get_current_value(const StringName &p_name) const {
	const int *idx_ptr = global_param_index.getptr(p_name);
	if (idx_ptr) {
		return global_params[*idx_ptr].current_value.load(std::memory_order_relaxed);
	}
	return 0.0f;
}

float RTPCEngine::get_target_value(const StringName &p_name) const {
	const int *idx_ptr = global_param_index.getptr(p_name);
	if (idx_ptr) {
		return global_params[*idx_ptr].target_value.load(std::memory_order_relaxed);
	}
	return 0.0f;
}

// --- Curve evaluation (static, can be called from any thread) ---

float RTPCEngine::evaluate_curve(const Ref<Curve> &p_curve, float p_input, float p_min_out, float p_max_out) const {
	float normalized;
	if (p_curve.is_valid()) {
		normalized = p_curve->sample(CLAMP(p_input, 0.0f, 1.0f));
	} else {
		// Linear fallback when no curve is provided
		normalized = CLAMP(p_input, 0.0f, 1.0f);
	}
	return p_min_out + normalized * (p_max_out - p_min_out);
}

// --- Configuration ---

void RTPCEngine::set_sample_rate(float p_rate) {
	sample_rate = p_rate;
	// Recompute all coefficients
	for (int i = 0; i < global_param_count; i++) {
		// Approximate: assume same smooth_time_ms (we don't store it per-param,
		// so use the default). For production, store smooth_time per param.
		global_params[i].smooth_coeff = _compute_coeff(default_smooth_time_ms);
	}
}

void RTPCEngine::set_default_smooth_time(float p_ms) {
	default_smooth_time_ms = MAX(0.1f, p_ms);
}

float RTPCEngine::_compute_coeff(float p_smooth_time_ms) const {
	// One-pole coefficient: how much to move per sample.
	// coeff = 1 - exp(-1 / (time_in_seconds * sample_rate))
	// This gives ~63% of the way there after smooth_time_ms.
	if (p_smooth_time_ms <= 0.0f) {
		return 1.0f; // Instant (no smoothing)
	}
	float time_sec = p_smooth_time_ms / 1000.0f;
	return 1.0f - expf(-1.0f / (time_sec * sample_rate));
}

// --- GDScript-friendly wrappers ---

void RTPCEngine::set_parameter_target(const String &p_name, float p_value) {
	set_target(StringName(p_name), p_value);
}

float RTPCEngine::get_parameter_value(const String &p_name) const {
	return get_current_value(StringName(p_name));
}

void RTPCEngine::register_global_parameter(const String &p_name, float p_default, float p_smooth_ms) {
	register_parameter(StringName(p_name), p_default, p_smooth_ms);
}

bool RTPCEngine::has_global_parameter(const String &p_name) const {
	return has_parameter(StringName(p_name));
}
