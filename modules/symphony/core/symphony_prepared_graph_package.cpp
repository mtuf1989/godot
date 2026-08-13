/**************************************************************************/
/*  symphony_prepared_graph_package.cpp                                   */
/**************************************************************************/

#include "symphony_prepared_graph_package.h"
#include "symphony_realtime_scope.h"

#include "core/os/memory.h"
#include "core/templates/hashfuncs.h"
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

struct FingerprintLess {
	_FORCE_INLINE_ bool operator()(const PreparedGraphPackage::OperatorFingerprint &p_a, const PreparedGraphPackage::OperatorFingerprint &p_b) const {
		return p_a.node_id < p_b.node_id;
	}
};

uint32_t _structural_hash(uint32_t p_type_hash, size_t p_state_bytes) {
	return hash_murmur3_one_32((uint32_t)p_state_bytes, p_type_hash);
}

} // namespace

PreparedGraphPackage *PreparedGraphPackage::create_from_graph(CompiledGraph *p_graph, size_t p_arena_bytes, size_t p_total_bytes, int p_lod_tier, float p_estimated_cost_units) {
	symphony_rt_note(SymphonyRTViolation::Alloc, "PreparedGraphPackage::create_from_graph");
	symphony_rt_note(SymphonyRTViolation::ContainerMutation, "PreparedGraphPackage::create_from_graph");
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

		if (p_graph->node_ids && p_graph->operator_types) {
			OperatorFingerprint fp;
			fp.node_id = p_graph->node_ids[i];
			fp.type_hash = p_graph->operator_types[i].hash();
			const size_t state_bytes = op->export_state(nullptr, 0);
			fp.structural_hash = _structural_hash(fp.type_hash, state_bytes);
			fp.exec_index = i;
			pkg->fingerprints.push_back(fp);
		}
	}

	pkg->param_routes.sort_custom<ParamRouteLess>();
	pkg->trigger_routes.sort_custom<TriggerRouteLess>();
	pkg->fingerprints.sort_custom<FingerprintLess>();
	return pkg;
}

void PreparedGraphPackage::destroy(PreparedGraphPackage *p_package) {
	symphony_rt_note(SymphonyRTViolation::Free, "PreparedGraphPackage::destroy");
	if (!p_package) {
		return;
	}
	if (p_package->graph) {
		memdelete(p_package->graph);
		p_package->graph = nullptr;
	}
	memdelete(p_package);
}

void PreparedGraphPackage::migrate_compatible_state(const PreparedGraphPackage *p_from, PreparedGraphPackage *p_to) {
	if (!p_from || !p_to || !p_from->graph || !p_to->graph) {
		return;
	}
	CompiledGraph *from_graph = p_from->graph;
	CompiledGraph *to_graph = p_to->graph;
	uint8_t state_buf[256];

	for (int i = 0; i < p_from->fingerprints.size(); i++) {
		const OperatorFingerprint &src_fp = p_from->fingerprints[i];
		if (src_fp.exec_index < 0 || src_fp.exec_index >= from_graph->operator_count) {
			continue;
		}
		const OperatorFingerprint *dst_fp = p_to->find_fingerprint(src_fp.node_id);
		if (!dst_fp) {
			continue;
		}
		if (dst_fp->type_hash != src_fp.type_hash || dst_fp->structural_hash != src_fp.structural_hash) {
			continue;
		}
		if (dst_fp->exec_index < 0 || dst_fp->exec_index >= to_graph->operator_count) {
			continue;
		}

		SymphonyOperator *src_op = from_graph->operators[src_fp.exec_index];
		SymphonyOperator *dst_op = to_graph->operators[dst_fp->exec_index];
		if (!src_op || !dst_op) {
			continue;
		}

		const size_t state_size = src_op->export_state(nullptr, 0);
		// Skip empty or large histories (delay/granular/spectral leave via transition fade).
		if (state_size == 0 || state_size > sizeof(state_buf)) {
			continue;
		}
		if (dst_op->export_state(nullptr, 0) != state_size) {
			continue;
		}
		if (src_op->export_state(state_buf, sizeof(state_buf)) != state_size) {
			continue;
		}
		dst_op->import_state(state_buf, state_size);
	}
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

const PreparedGraphPackage::OperatorFingerprint *PreparedGraphPackage::find_fingerprint(int32_t p_node_id) const {
	int lo = 0;
	int hi = fingerprints.size() - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		const int32_t mid_id = fingerprints[mid].node_id;
		if (mid_id == p_node_id) {
			return &fingerprints[mid];
		}
		if (mid_id < p_node_id) {
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return nullptr;
}
