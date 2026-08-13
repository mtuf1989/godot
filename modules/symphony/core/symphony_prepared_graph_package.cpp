/**************************************************************************/
/*  symphony_prepared_graph_package.cpp                                   */
/**************************************************************************/

#include "symphony_prepared_graph_package.h"

#include "core/os/memory.h"
#include "core/templates/sort_array.h"

namespace {

struct ParamRouteLess {
	_FORCE_INLINE_ bool operator()(const PreparedGraphPackage::ParamRoute &p_a, const PreparedGraphPackage::ParamRoute &p_b) const {
		return StringName::AlphCompare()(p_a.name, p_b.name);
	}
};

struct TriggerRouteLess {
	_FORCE_INLINE_ bool operator()(const PreparedGraphPackage::TriggerRoute &p_a, const PreparedGraphPackage::TriggerRoute &p_b) const {
		return StringName::AlphCompare()(p_a.name, p_b.name);
	}
};

} // namespace

PreparedGraphPackage *PreparedGraphPackage::create_from_graph(CompiledGraph *p_graph, size_t p_arena_bytes, size_t p_total_bytes, int p_lod_tier, float p_estimated_cost_units) {
	ERR_FAIL_NULL_V(p_graph, nullptr);

	PreparedGraphPackage *pkg = memnew(PreparedGraphPackage);
	pkg->graph = p_graph;
	pkg->arena_bytes = p_arena_bytes ? p_arena_bytes : p_graph->arena.capacity;
	pkg->total_package_bytes = p_total_bytes ? p_total_bytes : pkg->arena_bytes;
	pkg->estimated_cost_units = p_estimated_cost_units > 0.0f ? p_estimated_cost_units : p_graph->estimated_cost_units;
	pkg->lod_tier = p_lod_tier;

	for (int32_t i = 0; i < p_graph->operator_count; i++) {
		SymphonyOperator *op = p_graph->operators[i];
		if (!op) {
			continue;
		}
		if (auto *gout = dynamic_cast<SymphonyGraphOutput *>(op)) {
			pkg->graph_output = gout;
		}
		if (auto *gin = dynamic_cast<SymphonyGraphInput *>(op)) {
			StringName name = p_graph->node_names ? p_graph->node_names[i] : StringName();
			if (name != StringName()) {
				ParamRoute route;
				route.name = name;
				route.input = gin;
				pkg->param_routes.push_back(route);
			}
		}
		if (auto *tin = dynamic_cast<SymphonyTriggerInput *>(op)) {
			StringName name = p_graph->node_names ? p_graph->node_names[i] : StringName();
			if (name != StringName()) {
				TriggerRoute route;
				route.name = name;
				route.input = tin;
				pkg->trigger_routes.push_back(route);
			}
		}
	}

	pkg->param_routes.sort_custom<ParamRouteLess>();
	pkg->trigger_routes.sort_custom<TriggerRouteLess>();
	return pkg;
}

void PreparedGraphPackage::destroy(PreparedGraphPackage *p_package) {
	if (!p_package) {
		return;
	}
	if (p_package->graph) {
		memdelete(p_package->graph);
		p_package->graph = nullptr;
	}
	memdelete(p_package);
}

SymphonyGraphInput *PreparedGraphPackage::find_param(const StringName &p_name) const {
	int lo = 0;
	int hi = param_routes.size() - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		const StringName &mid_name = param_routes[mid].name;
		if (mid_name == p_name) {
			return param_routes[mid].input;
		}
		if (StringName::AlphCompare()(mid_name, p_name)) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return nullptr;
}

SymphonyTriggerInput *PreparedGraphPackage::find_trigger(const StringName &p_name) const {
	int lo = 0;
	int hi = trigger_routes.size() - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		const StringName &mid_name = trigger_routes[mid].name;
		if (mid_name == p_name) {
			return trigger_routes[mid].input;
		}
		if (StringName::AlphCompare()(mid_name, p_name)) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return nullptr;
}
