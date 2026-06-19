#pragma once

#include "core/io/resource.h"

class CIPModifier : public Resource {
	GDCLASS(CIPModifier, Resource);

protected:
	static void _bind_methods();
	GDVIRTUAL2RC(Variant, _modify, Variant, double)

public:
	virtual Variant modify(const Variant &p_value, double p_delta) const;
};
