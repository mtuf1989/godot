#include "rtpc_engine.h"
#include "../core/symphony_pin_types.h"
#include "../core/symphony_realtime_scope.h"
#include "core/object/class_db.h"
#include "servers/audio/audio_server.h"

#include <cmath>

RTPCEngine *RTPCEngine::singleton = nullptr;

RTPCEngine::RTPCEngine() {
	singleton = this;
	if (AudioServer::get_singleton()) {
		sample_rate = AudioServer::get_singleton()->get_mix_rate();
		AudioServer::get_singleton()->add_mix_callback(_mix_callback, this);
		mix_callback_registered = true;
	} else {
		sample_rate = 48000.0f;
		mix_callback_registered = false;
	}
}

RTPCEngine::~RTPCEngine() {
	if (mix_callback_registered && AudioServer::get_singleton()) {
		AudioServer::get_singleton()->remove_mix_callback(_mix_callback, this);
	}
	singleton = nullptr;
}

void RTPCEngine::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parameter_target", "name", "value"), &RTPCEngine::set_parameter_target);
	ClassDB::bind_method(D_METHOD("get_parameter_value", "name"), &RTPCEngine::get_parameter_value);
	ClassDB::bind_method(D_METHOD("register_global_parameter", "name", "default_value", "smooth_ms"), &RTPCEngine::register_global_parameter, DEFVAL(0.0f), DEFVAL(DEFAULT_SMOOTH_TIME_MS));
	ClassDB::bind_method(D_METHOD("has_global_parameter", "name"), &RTPCEngine::has_global_parameter);
	ClassDB::bind_method(D_METHOD("find_global_parameter", "name"), &RTPCEngine::find_global_parameter);
	ClassDB::bind_method(D_METHOD("set_parameter_target_by_handle", "handle", "value"), &RTPCEngine::set_parameter_target_by_handle);
	ClassDB::bind_method(D_METHOD("get_parameter_value_by_handle", "handle"), &RTPCEngine::get_parameter_value_by_handle);
	ClassDB::bind_method(D_METHOD("get_parameter_count"), &RTPCEngine::get_parameter_count);
	ClassDB::bind_method(D_METHOD("get_missing_handle_count"), &RTPCEngine::get_missing_handle_count);
	ClassDB::bind_method(D_METHOD("set_default_smooth_time", "ms"), &RTPCEngine::set_default_smooth_time);
	ClassDB::bind_method(D_METHOD("get_default_smooth_time"), &RTPCEngine::get_default_smooth_time);
	ClassDB::bind_method(D_METHOD("evaluate_curve", "curve", "input", "min_out", "max_out"), &RTPCEngine::evaluate_curve);

	ClassDB::bind_method(D_METHOD("register_analysis", "name"), &RTPCEngine::register_analysis);
	ClassDB::bind_method(D_METHOD("set_analysis", "name", "value"), &RTPCEngine::set_analysis);
	ClassDB::bind_method(D_METHOD("get_analysis", "name"), &RTPCEngine::get_analysis);
	ClassDB::bind_method(D_METHOD("has_analysis", "name"), &RTPCEngine::has_analysis);
	ClassDB::bind_method(D_METHOD("find_analysis", "name"), &RTPCEngine::find_analysis);
	ClassDB::bind_method(D_METHOD("set_analysis_by_handle", "handle", "value"), &RTPCEngine::set_analysis_by_handle);
	ClassDB::bind_method(D_METHOD("get_analysis_by_handle", "handle"), &RTPCEngine::get_analysis_by_handle);
	ClassDB::bind_method(D_METHOD("get_analysis_output_count"), &RTPCEngine::get_analysis_output_count);
	ClassDB::bind_method(D_METHOD("get_analysis_name_at", "index"), &RTPCEngine::get_analysis_name_at);
	ClassDB::bind_method(D_METHOD("get_analysis_value_at", "index"), &RTPCEngine::get_analysis_value_at);
}

void RTPCEngine::_mix_callback(void *p_userdata) {
	SymphonyRealtimeScope rt_scope;
	RTPCEngine *engine = static_cast<RTPCEngine *>(p_userdata);
	int frames = AudioServer::get_singleton()->thread_get_mix_buffer_size();
	engine->smooth_all(frames);
}

float RTPCEngine::_compute_block_alpha(float p_smooth_time_ms, int p_num_frames) const {
	if (p_smooth_time_ms <= 0.0f || p_num_frames <= 0) {
		return 1.0f;
	}
	float time_sec = p_smooth_time_ms / 1000.0f;
	return 1.0f - expf(-(float)p_num_frames / (time_sec * sample_rate));
}

void RTPCEngine::_recompute_block_alphas(int p_num_frames) {
	for (int i = 0; i < global_param_count; i++) {
		if (global_params[i].active) {
			global_params[i].block_alpha = _compute_block_alpha(global_params[i].smooth_time_ms, p_num_frames);
		}
	}
	cached_mix_frames = p_num_frames;
}

void RTPCEngine::smooth_all(int p_num_frames) {
	if (p_num_frames != cached_mix_frames) {
		_recompute_block_alphas(p_num_frames);
	}
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
			param.current_value.store(current + param.block_alpha * diff, std::memory_order_relaxed);
		}
	}
}

RTPCEngine::Handle RTPCEngine::register_parameter(const StringName &p_name, float p_default_value, float p_smooth_time_ms) {
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "RTPCEngine::register_parameter");
	ERR_FAIL_COND_V_MSG(global_param_count >= MAX_GLOBAL_PARAMETERS, INVALID_HANDLE,
			"RTPCEngine: Maximum global parameter count reached.");
	ERR_FAIL_COND_V_MSG(global_param_index.has(p_name), INVALID_HANDLE,
			"RTPCEngine: Parameter '" + String(p_name) + "' already registered.");

	Handle idx = global_param_count++;
	global_params[idx].name = p_name;
	global_params[idx].current_value.store(p_default_value, std::memory_order_relaxed);
	global_params[idx].target_value.store(p_default_value, std::memory_order_relaxed);
	global_params[idx].smooth_time_ms = MAX(0.0f, p_smooth_time_ms);
	global_params[idx].block_alpha = _compute_block_alpha(global_params[idx].smooth_time_ms, cached_mix_frames > 0 ? cached_mix_frames : SYMPHONY_MICRO_BLOCK_SIZE);
	global_params[idx].active = true;
	global_param_index[p_name] = idx;
	return idx;
}

RTPCEngine::Handle RTPCEngine::find_parameter(const StringName &p_name) const {
	const Handle *idx = global_param_index.getptr(p_name);
	return idx ? *idx : INVALID_HANDLE;
}

bool RTPCEngine::set_target(Handle p_handle, float p_value) {
	if (p_handle < 0 || p_handle >= global_param_count || !global_params[p_handle].active) {
		missing_handle_count.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	global_params[p_handle].target_value.store(p_value, std::memory_order_relaxed);
	return true;
}

bool RTPCEngine::set_target(const StringName &p_name, float p_value) {
	Handle h = find_parameter(p_name);
	if (h == INVALID_HANDLE) {
		missing_handle_count.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	return set_target(h, p_value);
}

bool RTPCEngine::has_parameter(const StringName &p_name) const {
	return global_param_index.has(p_name);
}

float RTPCEngine::get_current_value(Handle p_handle) const {
	if (p_handle < 0 || p_handle >= global_param_count || !global_params[p_handle].active) {
		return 0.0f;
	}
	return global_params[p_handle].current_value.load(std::memory_order_relaxed);
}

float RTPCEngine::get_current_value(const StringName &p_name) const {
	return get_current_value(find_parameter(p_name));
}

float RTPCEngine::get_target_value(Handle p_handle) const {
	if (p_handle < 0 || p_handle >= global_param_count || !global_params[p_handle].active) {
		return 0.0f;
	}
	return global_params[p_handle].target_value.load(std::memory_order_relaxed);
}

float RTPCEngine::get_target_value(const StringName &p_name) const {
	return get_target_value(find_parameter(p_name));
}

float RTPCEngine::evaluate_curve(const Ref<Curve> &p_curve, float p_input, float p_min_out, float p_max_out) const {
	float normalized;
	if (p_curve.is_valid()) {
		normalized = p_curve->sample(CLAMP(p_input, 0.0f, 1.0f));
	} else {
		normalized = CLAMP(p_input, 0.0f, 1.0f);
	}
	return p_min_out + normalized * (p_max_out - p_min_out);
}

void RTPCEngine::set_sample_rate(float p_rate) {
	sample_rate = p_rate;
	if (cached_mix_frames > 0) {
		_recompute_block_alphas(cached_mix_frames);
	}
}

void RTPCEngine::set_default_smooth_time(float p_ms) {
	default_smooth_time_ms = MAX(0.1f, p_ms);
}

uint64_t RTPCEngine::get_missing_handle_count() const {
	return missing_handle_count.load(std::memory_order_relaxed);
}

bool RTPCEngine::set_parameter_target(const String &p_name, float p_value) {
	return set_target(StringName(p_name), p_value);
}

float RTPCEngine::get_parameter_value(const String &p_name) const {
	return get_current_value(StringName(p_name));
}

RTPCEngine::Handle RTPCEngine::register_global_parameter(const String &p_name, float p_default, float p_smooth_ms) {
	return register_parameter(StringName(p_name), p_default, p_smooth_ms);
}

bool RTPCEngine::has_global_parameter(const String &p_name) const {
	return has_parameter(StringName(p_name));
}

RTPCEngine::Handle RTPCEngine::register_analysis_output(const StringName &p_name) {
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "RTPCEngine::register_analysis_output");
	ERR_FAIL_COND_V_MSG(analysis_output_count >= MAX_ANALYSIS_OUTPUTS, INVALID_HANDLE,
			"RTPCEngine: Maximum analysis output count reached.");
	ERR_FAIL_COND_V_MSG(analysis_output_index.has(p_name), INVALID_HANDLE,
			"RTPCEngine: Analysis output '" + String(p_name) + "' already registered.");

	Handle idx = analysis_output_count++;
	analysis_outputs[idx].name = p_name;
	analysis_outputs[idx].value.store(0.0f, std::memory_order_relaxed);
	analysis_outputs[idx].active = true;
	analysis_output_index[p_name] = idx;
	return idx;
}

RTPCEngine::Handle RTPCEngine::find_analysis_output(const StringName &p_name) const {
	const Handle *idx = analysis_output_index.getptr(p_name);
	return idx ? *idx : INVALID_HANDLE;
}

bool RTPCEngine::set_analysis_value(Handle p_handle, float p_value) {
	if (p_handle < 0 || p_handle >= analysis_output_count || !analysis_outputs[p_handle].active) {
		missing_handle_count.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	analysis_outputs[p_handle].value.store(p_value, std::memory_order_relaxed);
	return true;
}

bool RTPCEngine::set_analysis_value(const StringName &p_name, float p_value) {
	Handle h = find_analysis_output(p_name);
	if (h == INVALID_HANDLE) {
		missing_handle_count.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	return set_analysis_value(h, p_value);
}

float RTPCEngine::get_analysis_value(Handle p_handle) const {
	if (p_handle < 0 || p_handle >= analysis_output_count || !analysis_outputs[p_handle].active) {
		return 0.0f;
	}
	return analysis_outputs[p_handle].value.load(std::memory_order_relaxed);
}

float RTPCEngine::get_analysis_value(const StringName &p_name) const {
	return get_analysis_value(find_analysis_output(p_name));
}

bool RTPCEngine::has_analysis_output(const StringName &p_name) const {
	return analysis_output_index.has(p_name);
}

StringName RTPCEngine::get_analysis_output_name(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, analysis_output_count, StringName());
	return analysis_outputs[p_index].name;
}

float RTPCEngine::get_analysis_output_value_by_index(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, analysis_output_count, 0.0f);
	return analysis_outputs[p_index].value.load(std::memory_order_relaxed);
}

RTPCEngine::Handle RTPCEngine::register_analysis(const String &p_name) {
	return register_analysis_output(StringName(p_name));
}

bool RTPCEngine::set_analysis(const String &p_name, float p_value) {
	return set_analysis_value(StringName(p_name), p_value);
}

float RTPCEngine::get_analysis(const String &p_name) const {
	return get_analysis_value(StringName(p_name));
}

bool RTPCEngine::has_analysis(const String &p_name) const {
	return has_analysis_output(StringName(p_name));
}

String RTPCEngine::get_analysis_name_at(int p_index) const {
	return String(get_analysis_output_name(p_index));
}

float RTPCEngine::get_analysis_value_at(int p_index) const {
	return get_analysis_output_value_by_index(p_index);
}

int RTPCEngine::find_global_parameter(const String &p_name) const {
	return find_parameter(StringName(p_name));
}

bool RTPCEngine::set_parameter_target_by_handle(int p_handle, float p_value) {
	return set_target((Handle)p_handle, p_value);
}

float RTPCEngine::get_parameter_value_by_handle(int p_handle) const {
	return get_current_value((Handle)p_handle);
}

int RTPCEngine::find_analysis(const String &p_name) const {
	return find_analysis_output(StringName(p_name));
}

bool RTPCEngine::set_analysis_by_handle(int p_handle, float p_value) {
	return set_analysis_value((Handle)p_handle, p_value);
}

float RTPCEngine::get_analysis_by_handle(int p_handle) const {
	return get_analysis_value((Handle)p_handle);
}
