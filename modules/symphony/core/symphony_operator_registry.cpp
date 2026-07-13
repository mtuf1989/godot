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

void OperatorRegistry::get_registered_types(Vector<StringName> &r_types) const {
	for (const auto &E : descriptors) {
		r_types.push_back(E.key);
	}
}
