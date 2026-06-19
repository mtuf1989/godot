#include "cip_mapping_context.h"

#include "core/object/class_db.h"

void CIPMappingContext::set_priority(int p_priority) {
	priority = p_priority;
}

int CIPMappingContext::get_priority() const {
	return priority;
}

void CIPMappingContext::set_mappings(const TypedArray<CIPActionMapping> &p_mappings) {
	mappings = p_mappings;
}

TypedArray<CIPActionMapping> CIPMappingContext::get_mappings() const {
	return mappings;
}

void CIPMappingContext::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &CIPMappingContext::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &CIPMappingContext::get_priority);
	ClassDB::bind_method(D_METHOD("set_mappings", "mappings"), &CIPMappingContext::set_mappings);
	ClassDB::bind_method(D_METHOD("get_mappings"), &CIPMappingContext::get_mappings);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "mappings", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("CIPActionMapping")), "set_mappings", "get_mappings");
}
