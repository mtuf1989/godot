#include "symphony_operator_registry.h"

OperatorRegistry *OperatorRegistry::singleton = nullptr;

OperatorRegistry *OperatorRegistry::get_singleton() {
	return singleton;
}

void OperatorRegistry::create_singleton() {
	singleton = memnew(OperatorRegistry);
}

void OperatorRegistry::destroy_singleton() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

void OperatorRegistry::register_operator(const OperatorDescriptor &p_desc) {
	descriptors.insert(p_desc.type_name, p_desc);
}

void OperatorRegistry::register_alias(const StringName &p_old_name, const StringName &p_current_name) {
	aliases.insert(p_old_name, p_current_name);
}

const OperatorDescriptor *OperatorRegistry::find(const StringName &p_type_name) const {
	auto it = descriptors.find(p_type_name);
	if (it != descriptors.end()) {
		return &it->value;
	}
	// Fallback: check alias map for deprecated names (dev-log #15).
	auto alias_it = aliases.find(p_type_name);
	if (alias_it != aliases.end()) {
		const StringName &resolved = alias_it->value;
		auto resolved_it = descriptors.find(resolved);
		if (resolved_it != descriptors.end()) {
			WARN_PRINT(vformat("Symphony: Operator '%s' is deprecated. Use '%s' instead. "
					"Update your .tres files to silence this warning.",
					String(p_type_name), String(resolved)));
			return &resolved_it->value;
		}
	}
	return nullptr;
}

OperatorDescriptor *OperatorRegistry::find_mut(const StringName &p_type_name) {
	auto it = descriptors.find(p_type_name);
	if (it != descriptors.end()) {
		return &it->value;
	}
	return nullptr;
}

void OperatorRegistry::mark_resettable_operators() {
	// P10 initially-resettable set (tech-fix-plan). export_state_bytes is an upper bound
	// for arena pristine planning; 0 means no pristine blob (stateless or empty export).
	struct Entry {
		const char *name;
		size_t export_bytes;
	};
	static const Entry k_entries[] = {
		{ "Oscillator", 8 },
		{ "FMOscillator", 32 },
		{ "FormantOsc", 32 },
		{ "Noise", 44 },
		{ "LFO", 16 },
		{ "WavePlayer", 16 },
		{ "ADSR", 16 },
		{ "EnvelopeFloat", 16 },
		{ "BiquadFilter", 56 },
		{ "SVFilter", 32 },
		{ "OnePole", 8 },
		{ "DCBlocker", 8 },
		{ "Saturator", 96 },
		{ "Waveshaper", 96 },
		{ "Gain", 0 },
		{ "Constant", 0 },
		{ "MathAdd", 0 },
		{ "Mix", 0 },
		{ "MapRange", 0 },
		{ "SampleHold", 16 },
		{ "RingMod", 0 },
		{ "CrossFade", 0 },
		{ "ParameterSmoother", 16 },
		{ "EnvelopeFollower", 16 },
		{ "FrequencyEnvelopeFollower", 64 },
		// ModalBank sets resettable + export_state_bytes in its own register_operator.
	};
	for (const Entry &e : k_entries) {
		if (OperatorDescriptor *desc = find_mut(StringName(e.name))) {
			desc->resettable = true;
			if (desc->export_state_bytes == 0) {
				desc->export_state_bytes = e.export_bytes;
			}
		}
	}
}

void OperatorRegistry::get_registered_types(Vector<StringName> &r_types) const {
	for (const auto &E : descriptors) {
		r_types.push_back(E.key);
	}
}
