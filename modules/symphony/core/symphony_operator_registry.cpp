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

const OperatorDescriptor *OperatorRegistry::find(const StringName &p_type_name) const {
	auto it = descriptors.find(p_type_name);
	if (it != descriptors.end()) {
		return &it->value;
	}
	return nullptr;
}

void OperatorRegistry::get_registered_types(Vector<StringName> &r_types) const {
	for (const auto &E : descriptors) {
		r_types.push_back(E.key);
	}
}
