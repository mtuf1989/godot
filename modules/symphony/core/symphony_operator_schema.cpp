#include "symphony_operator_registry.h"

#include "core/variant/array.h"

#include <initializer_list>

namespace {

ParamDescriptor *find_param(OperatorDescriptor &p_desc, const StringName &p_name) {
	for (ParamDescriptor &param : p_desc.params) {
		if (param.name == p_name) {
			return &param;
		}
	}
	return nullptr;
}

void ensure_param(OperatorDescriptor &p_desc, const ParamDescriptor &p_param) {
	if (find_param(p_desc, p_param.name) == nullptr) {
		p_desc.params.push_back(p_param);
	}
}

void tag(OperatorDescriptor &p_desc, const StringName &p_name, ParamValueType p_type, const String &p_unit = String()) {
	ParamDescriptor *param = find_param(p_desc, p_name);
	if (param == nullptr) {
		return;
	}
	param->value_type = p_type;
	if (!p_unit.is_empty()) {
		param->unit = p_unit;
	}
}

void tag_enum(OperatorDescriptor &p_desc, const StringName &p_name, std::initializer_list<const char *> p_values) {
	ParamDescriptor *param = find_param(p_desc, p_name);
	if (param == nullptr) {
		return;
	}
	param->value_type = ParamValueType::ENUM;
	param->enum_values.clear();
	for (const char *value : p_values) {
		param->enum_values.push_back(String(value));
	}
}

String pin_type_name(SymphonyPinType p_type) {
	switch (p_type) {
		case SymphonyPinType::AUDIO:
			return "audio";
		case SymphonyPinType::FLOAT:
			return "float";
		case SymphonyPinType::INT:
			return "int";
		case SymphonyPinType::BOOL:
			return "bool";
		case SymphonyPinType::TRIGGER:
			return "trigger";
	}
	return "audio";
}

String value_type_name(ParamValueType p_type) {
	switch (p_type) {
		case ParamValueType::FLOAT:
			return "float";
		case ParamValueType::INT:
			return "int";
		case ParamValueType::BOOL:
			return "bool";
		case ParamValueType::STRING:
			return "string";
		case ParamValueType::ENUM:
			return "enum";
		case ParamValueType::FLOAT_ARRAY:
			return "float_array";
		case ParamValueType::RESOURCE_PATH:
			return "resource_path";
	}
	return "float";
}

struct NameSort {
	bool operator()(const StringName &p_a, const StringName &p_b) const {
		return String(p_a).nocasecmp_to(String(p_b)) < 0;
	}
};

} // namespace

void OperatorRegistry::annotate_authoring_schema() {
	Vector<StringName> types;
	get_registered_types(types);
	for (const StringName &type_name : types) {
		OperatorDescriptor *desc = descriptors.getptr(type_name);
		if (desc == nullptr) {
			continue;
		}
		if (type_name == StringName("SubGraph")) {
			desc->dynamic_pins = true;
		}
		if (type_name == StringName("TriggerInput")) {
			ParamDescriptor trigger_name;
			trigger_name.name = "trigger_name";
			trigger_name.value_type = ParamValueType::STRING;
			ensure_param(*desc, trigger_name);
		}
		if (type_name == StringName("ModalBank")) {
			ParamDescriptor frequencies;
			frequencies.name = "frequencies";
			frequencies.value_type = ParamValueType::FLOAT_ARRAY;
			frequencies.unit = "Hz";
			ParamDescriptor decays;
			decays.name = "decay_times";
			decays.value_type = ParamValueType::FLOAT_ARRAY;
			decays.unit = "s";
			ParamDescriptor gains;
			gains.name = "gains";
			gains.value_type = ParamValueType::FLOAT_ARRAY;
			ensure_param(*desc, frequencies);
			ensure_param(*desc, decays);
			ensure_param(*desc, gains);
		}

		tag(*desc, "parameter_name", ParamValueType::STRING);
		tag(*desc, "display_name", ParamValueType::STRING);
		tag(*desc, "trigger_name", ParamValueType::STRING);
		tag(*desc, "resource_path", ParamValueType::RESOURCE_PATH);
		tag(*desc, "frequency", ParamValueType::FLOAT, "Hz");
		tag(*desc, "cutoff", ParamValueType::FLOAT, "Hz");
		tag(*desc, "rate", ParamValueType::FLOAT, "Hz");
		tag(*desc, "attack", ParamValueType::FLOAT, "s");
		tag(*desc, "decay", ParamValueType::FLOAT, "s");
		tag(*desc, "release", ParamValueType::FLOAT, "s");
		tag(*desc, "delay", ParamValueType::FLOAT, "s");
		tag(*desc, "predelay", ParamValueType::FLOAT, "s");
		tag(*desc, "smooth_time", ParamValueType::FLOAT, "s");
		tag(*desc, "frequencies", ParamValueType::FLOAT_ARRAY, "Hz");
		tag(*desc, "decay_times", ParamValueType::FLOAT_ARRAY, "s");
		tag(*desc, "gains", ParamValueType::FLOAT_ARRAY);
		tag_enum(*desc, "waveform", { "sine", "saw", "square", "triangle", "pwm" });
		tag_enum(*desc, "pin_type", { "audio", "float", "int", "bool", "trigger" });
		tag_enum(*desc, "loop_mode", { "none", "forward", "ping_pong" });
		if (type_name == StringName("Noise")) {
			tag_enum(*desc, "mode", { "white", "pink" });
		}

		for (ParamDescriptor &param : desc->params) {
			if (param.value_type != ParamValueType::FLOAT) {
				continue;
			}
			const String name = String(param.name);
			if ((name.begins_with("auto_") || name == "clamp" || name == "bake_audio" || name == "loop") && param.min_value >= 0.0f && param.max_value <= 1.0f) {
				param.value_type = ParamValueType::BOOL;
			}
		}
	}
}

Dictionary OperatorRegistry::get_operator_schema() const {
	Vector<StringName> types;
	get_registered_types(types);
	types.sort_custom<NameSort>();

	Array operators;
	for (const StringName &type_name : types) {
		const OperatorDescriptor *desc = find(type_name);
		if (desc == nullptr) {
			continue;
		}
		Dictionary op;
		op["type"] = String(desc->type_name);
		op["category"] = desc->category;
		op["dynamic_pins"] = desc->dynamic_pins;
		Array inputs;
		for (const PinDescriptor &pin : desc->inputs) {
			Dictionary entry;
			entry["name"] = String(pin.name);
			entry["type"] = pin_type_name(pin.type);
			entry["required"] = pin.required;
			entry["dynamic"] = pin.dynamic;
			inputs.push_back(entry);
		}
		Array outputs;
		for (const PinDescriptor &pin : desc->outputs) {
			Dictionary entry;
			entry["name"] = String(pin.name);
			entry["type"] = pin_type_name(pin.type);
			entry["dynamic"] = pin.dynamic;
			outputs.push_back(entry);
		}
		Array params;
		for (const ParamDescriptor &param : desc->params) {
			Dictionary entry;
			entry["name"] = String(param.name);
			entry["type"] = value_type_name(param.value_type);
			entry["default"] = param.default_value;
			entry["min"] = param.min_value;
			entry["max"] = param.max_value;
			entry["step"] = param.step;
			entry["unit"] = param.unit;
			Array enums;
			for (const String &value : param.enum_values) {
				enums.push_back(value);
			}
			entry["enum"] = enums;
			params.push_back(entry);
		}
		op["inputs"] = inputs;
		op["outputs"] = outputs;
		op["params"] = params;
		operators.push_back(op);
	}

	Dictionary schema;
	schema["schema_version"] = 2;
	Array stream_properties;
	Dictionary duration;
	duration["name"] = "duration_limit_seconds";
	duration["type"] = "float";
	duration["unit"] = "s";
	duration["default"] = 0.0;
	stream_properties.push_back(duration);
	schema["stream_properties"] = stream_properties;
	schema["operators"] = operators;
	return schema;
}
