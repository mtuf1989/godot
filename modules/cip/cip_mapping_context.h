#pragma once

#include "cip_action_mapping.h"
#include "core/io/resource.h"
#include "core/variant/typed_array.h"

class CIPMappingContext : public Resource {
	GDCLASS(CIPMappingContext, Resource);

private:
	int priority = 0;
	TypedArray<CIPActionMapping> mappings;

protected:
	static void _bind_methods();

public:
	void set_priority(int p_priority);
	int get_priority() const;

	void set_mappings(const TypedArray<CIPActionMapping> &p_mappings);
	TypedArray<CIPActionMapping> get_mappings() const;
};
