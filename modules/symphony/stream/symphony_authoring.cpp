#include "audio_stream_symphony.h"

#include "audio_stream_playback_symphony.h"
#include "../core/symphony_operator_registry.h"
#include "../core/symphony_render_seed.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/math/math_funcs.h"
#include "servers/audio/audio_frame.h"

#include <cmath>

namespace {

const ParamDescriptor *find_param(const OperatorDescriptor *p_desc, const StringName &p_name) {
	if (p_desc == nullptr) {
		return nullptr;
	}
	for (const ParamDescriptor &param : p_desc->params) {
		if (param.name == p_name) {
			return &param;
		}
	}
	return nullptr;
}

bool variant_matches(ParamValueType p_type, const Variant &p_value) {
	switch (p_type) {
		case ParamValueType::STRING:
		case ParamValueType::RESOURCE_PATH:
			return p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME;
		case ParamValueType::FLOAT:
		case ParamValueType::INT:
		case ParamValueType::ENUM:
			return p_value.get_type() == Variant::FLOAT || p_value.get_type() == Variant::INT;
		case ParamValueType::BOOL:
			return p_value.get_type() == Variant::BOOL || p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT;
		case ParamValueType::FLOAT_ARRAY:
			return p_value.get_type() == Variant::PACKED_FLOAT32_ARRAY || p_value.get_type() == Variant::ARRAY;
	}
	return false;
}

void write_wav(const String &p_path, const Vector<AudioFrame> &p_frames, int p_rate) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) {
		return;
	}
	const int32_t channels = 2;
	const int32_t bits = 16;
	const int32_t data_bytes = p_frames.size() * channels * (bits / 8);
	file->store_buffer((const uint8_t *)"RIFF", 4);
	file->store_32(36 + data_bytes);
	file->store_buffer((const uint8_t *)"WAVE", 4);
	file->store_buffer((const uint8_t *)"fmt ", 4);
	file->store_32(16);
	file->store_16(1);
	file->store_16(channels);
	file->store_32(p_rate);
	file->store_32(p_rate * channels * (bits / 8));
	file->store_16(channels * (bits / 8));
	file->store_16(bits);
	file->store_buffer((const uint8_t *)"data", 4);
	file->store_32(data_bytes);
	for (const AudioFrame &frame : p_frames) {
		float left = CLAMP(frame.left, -1.0f, 1.0f);
		float right = CLAMP(frame.right, -1.0f, 1.0f);
		file->store_16((int16_t)Math::round(left * 32767.0f));
		file->store_16((int16_t)Math::round(right * 32767.0f));
	}
}

double spectral_centroid(const Vector<AudioFrame> &p_frames, int p_rate) {
	const int count = MIN(1024, p_frames.size());
	if (count < 8 || p_rate <= 0) {
		return 0.0;
	}
	const int start = MAX(0, (p_frames.size() - count) / 2);
	double weighted = 0.0;
	double energy = 0.0;
	const int bins = count / 2;
	for (int k = 1; k < bins; k++) {
		double real = 0.0;
		double imag = 0.0;
		for (int n = 0; n < count; n++) {
			float sample = 0.5f * (p_frames[start + n].left + p_frames[start + n].right);
			double angle = -Math::TAU * (double)k * (double)n / (double)count;
			real += sample * Math::cos(angle);
			imag += sample * Math::sin(angle);
		}
		double magnitude = Math::sqrt(real * real + imag * imag);
		double frequency = (double)k * (double)p_rate / (double)count;
		weighted += frequency * magnitude;
		energy += magnitude;
	}
	return energy > 0.0 ? weighted / energy : 0.0;
}

} // namespace

Dictionary AudioStreamSymphony::get_operator_schema() {
	OperatorRegistry *registry = OperatorRegistry::get_singleton();
	if (registry == nullptr) {
		Dictionary empty;
		empty["schema_version"] = 2;
		empty["operators"] = Array();
		return empty;
	}
	return registry->get_operator_schema();
}

Dictionary AudioStreamSymphony::validate_authoring() const {
	const bool strict = schema_version >= 2;
	Array errors;
	Array warnings;
	auto push_diag = [&](const String &p_message, int p_node_id) {
		Dictionary diag;
		diag["message"] = p_message;
		diag["node_id"] = p_node_id;
		diag["resource_path"] = get_path();
		if (strict) {
			errors.push_back(diag);
		} else {
			warnings.push_back(diag);
		}
	};

	OperatorRegistry *registry = OperatorRegistry::get_singleton();
	HashMap<int64_t, int> input_owners;
	for (int i = 0; i < graph_desc.nodes.size(); i++) {
		const NodeDesc &node = graph_desc.nodes[i];
		const OperatorDescriptor *desc = registry ? registry->find(node.type_name) : nullptr;
		if (desc == nullptr) {
			push_diag(vformat("Unknown operator '%s'.", String(node.type_name)), node.id);
			continue;
		}
		for (const KeyValue<StringName, Variant> &entry : node.params) {
			const ParamDescriptor *param = find_param(desc, entry.key);
			if (param == nullptr) {
				push_diag(vformat("Unknown parameter '%s' on %s.", String(entry.key), String(node.type_name)), node.id);
				continue;
			}
			if (!variant_matches(param->value_type, entry.value)) {
				push_diag(vformat("Parameter '%s' on %s has the wrong type.", String(entry.key), String(node.type_name)), node.id);
			}
			if (param->value_type == ParamValueType::RESOURCE_PATH) {
				String path = entry.value;
				if (!path.is_empty() && !ResourceLoader::exists(path)) {
					push_diag(vformat("Unresolved resource path '%s'.", path), node.id);
				}
			}
		}
	}
	for (int i = 0; i < graph_desc.connections.size(); i++) {
		const ConnectionDesc &connection = graph_desc.connections[i];
		int64_t key = ((int64_t)connection.to_node << 32) | (uint32_t)connection.to_pin;
		if (input_owners.has(key)) {
			push_diag(vformat("Duplicate input connection on node %d pin %d. Use Mix or MathAdd.", connection.to_node, connection.to_pin), connection.to_node);
		} else {
			input_owners.insert(key, i);
		}
	}

	Dictionary compiled = validate_tier_compile(0);
	if (!bool(compiled.get("ok", false))) {
		Array compile_errors = compiled.get("errors", Array());
		for (int i = 0; i < compile_errors.size(); i++) {
			errors.push_back(compile_errors[i]);
		}
	}
	Array compile_warnings = compiled.get("warnings", Array());
	for (int i = 0; i < compile_warnings.size(); i++) {
		warnings.push_back(compile_warnings[i]);
	}

	Dictionary result;
	result["ok"] = errors.is_empty();
	result["errors"] = errors;
	result["warnings"] = warnings;
	result["schema_version"] = schema_version;
	return result;
}

Dictionary AudioStreamSymphony::render_offline(double p_duration_seconds, int64_t p_seed, const Dictionary &p_initial_parameters, const Array &p_triggers, const String &p_wav_path) {
	Dictionary result;
	result["ok"] = false;
	if (p_duration_seconds <= 0.0) {
		result["error"] = "duration must be positive";
		return result;
	}
	uint32_t seed = p_seed > 0 ? (uint32_t)p_seed : 1u;
	SymphonyRenderSeed::set(seed);

	Ref<AudioStreamPlayback> playback = instantiate_playback();
	AudioStreamPlaybackSymphony *symphony = Object::cast_to<AudioStreamPlaybackSymphony>(playback.ptr());
	if (symphony == nullptr) {
		SymphonyRenderSeed::set(0);
		result["error"] = "playback could not be created";
		return result;
	}
	symphony->start(0.0);
	Array parameter_names = p_initial_parameters.keys();
	for (int i = 0; i < parameter_names.size(); i++) {
		symphony->set_parameter(parameter_names[i], p_initial_parameters[parameter_names[i]]);
	}

	const int rate = MAX(1, (int)get_mix_rate());
	const int total_frames = MAX(1, (int)Math::round(p_duration_seconds * rate));
	Vector<AudioFrame> frames;
	frames.resize(total_frames);
	Vector<uint8_t> trigger_fired;
	trigger_fired.resize(p_triggers.size());
	for (int i = 0; i < trigger_fired.size(); i++) {
		trigger_fired.write[i] = 0;
	}

	int written = 0;
	double sum_squares = 0.0;
	float peak = 0.0f;
	int nonfinite = 0;
	int silent = 0;
	double cost_us = 0.0;
	AudioFrame block[256];
	while (written < total_frames && symphony->is_playing()) {
		int frames_now = MIN(256, total_frames - written);
		double block_end = (double)(written + frames_now) / (double)rate;
		for (int t = 0; t < p_triggers.size(); t++) {
			if (trigger_fired[t] != 0) {
				continue;
			}
			Dictionary trigger = p_triggers[t];
			double when = trigger.get("time", 0.0);
			if (when <= block_end) {
				symphony->trigger(trigger.get("name", StringName()), trigger.get("value", 1.0));
				trigger_fired.write[t] = 1;
			}
		}
		int produced = symphony->mix(block, 1.0f, frames_now);
		if (produced <= 0) {
			break;
		}
		cost_us += symphony->get_voice_cpu_microseconds();
		for (int i = 0; i < produced && written < total_frames; i++) {
			frames.write[written] = block[i];
			float sample = 0.5f * (block[i].left + block[i].right);
			if (!Math::is_finite(sample)) {
				nonfinite++;
				sample = 0.0f;
			}
			peak = MAX(peak, Math::abs(sample));
			if (Math::abs(sample) < 0.0001f) {
				silent++;
			}
			sum_squares += (double)sample * (double)sample;
			written++;
		}
	}
	if (written < frames.size()) {
		frames.resize(written);
	}
	SymphonyRenderSeed::set(0);

	const double rms = written > 0 ? Math::sqrt(sum_squares / (double)written) : 0.0;
	result["ok"] = true;
	result["peak"] = peak;
	result["rms"] = rms;
	result["silence"] = written > 0 ? (double)silent / (double)written : 1.0;
	result["nonfinite"] = nonfinite;
	result["spectral_centroid"] = spectral_centroid(frames, rate);
	result["render_cost_us"] = cost_us;
	result["frames"] = written;
	result["sample_rate"] = rate;
	result["seed"] = (int64_t)seed;
	if (!p_wav_path.is_empty() && written > 0) {
		write_wav(p_wav_path, frames, rate);
		result["wav_path"] = p_wav_path;
		String json_path = p_wav_path.get_basename() + ".json";
		Ref<FileAccess> json_file = FileAccess::open(json_path, FileAccess::WRITE);
		if (json_file.is_valid()) {
			json_file->store_string(JSON::stringify(result, "\t"));
			result["json_path"] = json_path;
		}
	}
	return result;
}
