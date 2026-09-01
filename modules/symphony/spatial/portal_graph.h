#ifndef PORTAL_GRAPH_H
#define PORTAL_GRAPH_H

#include "core/math/vector3.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"

// Portal graph + path solve (Task 14, Phase S6).
//
// The graph is ABSTRACT: rooms are integer node ids [0, node_count), portals are
// weighted undirected edges. This decouples the routing algorithm from the live
// scene tree (AcousticRoom3D / AcousticPortal3D), so it is fully unit-testable
// without a SceneTree/physics world and lets a baked-probe backend substitute
// for the runtime portal backend (deferred-notes requirement — keep the solve
// behind a virtual interface).
//
// A PortalGraphBuilder (engine-side, Task 15) fills this from the node registries.

// One portal edge between two rooms.
struct PortalEdge {
	int room_a = -1;
	int room_b = -1;
	float weight = 1.0f; // traversal cost (distance-based); lower = cheaper
	bool open = true; // closed edges are skipped by the solver
	Vector3 world_center; // portal centre (for apparent-position routing, Task 15)
	float aperture_area = 1.0f; // m² (Phase 6): folded as 1/area into `weight` so
	                            // Dijkstra prefers a wide arch over a small door
	                            // between the same room pair; also carried to the
	                            // hop for closest-point apparent-position.
	int portal_id = -1; // index back into the source portal registry (routing)

	int other(int p_room) const {
		if (p_room == room_a) {
			return room_b;
		}
		if (p_room == room_b) {
			return room_a;
		}
		return -1;
	}
};

// The abstract graph: node_count rooms + a list of edges.
class PortalGraph {
public:
	int node_count = 0;
	LocalVector<PortalEdge> edges;
	// Adjacency: node id → indices into `edges`.
	LocalVector<LocalVector<int>> adjacency;

	void clear() {
		node_count = 0;
		edges.clear();
		adjacency.clear();
	}

	void set_node_count(int p_count) {
		node_count = MAX(p_count, 0);
		adjacency.clear();
		adjacency.resize(node_count);
	}

	// Add an undirected weighted edge. Returns the edge index.
	int add_edge(int p_a, int p_b, float p_weight, bool p_open, const Vector3 &p_center = Vector3(), int p_portal_id = -1, float p_aperture_area = 1.0f);
};

// A solved path: the ordered list of edge indices traversed from a start room to
// a goal room, plus total cost. Empty edges + reachable=true means "same room".
struct PortalPath {
	LocalVector<int> edge_ids; // edges traversed, in order (start → goal)
	float total_cost = 0.0f;
	bool reachable = false;
};

// Virtual path-solve interface (so a baked backend can substitute).
class PortalPathSolver {
public:
	virtual ~PortalPathSolver() {}
	// Solve from p_start_room to p_goal_room over p_graph. Same room → empty
	// reachable path. Unreachable → reachable=false.
	virtual PortalPath solve(const PortalGraph &p_graph, int p_start_room, int p_goal_room) = 0;
};

// Dijkstra over the portal graph (open edges only).
class DijkstraPathSolver : public PortalPathSolver {
public:
	PortalPath solve(const PortalGraph &p_graph, int p_start_room, int p_goal_room) override;
};

// Path cache keyed by (start_room, goal_room), invalidated wholesale when the
// portal state epoch changes (open/close/move/add/remove — see
// AcousticPortal3D::get_state_epoch()).
class PortalPathCache {
private:
	// Phase 5.4: bound the cache so it can't grow O(rooms²) unbounded. Each
	// entry records a last-use tick; on overflow the least-recently-used entry
	// is evicted.
	struct Entry {
		PortalPath path;
		uint64_t last_use = 0;
	};
	HashMap<uint64_t, Entry> cache;
	uint64_t last_epoch = 0;
	uint64_t use_tick = 0;
	static constexpr int MAX_ENTRIES = 256;

	static uint64_t _key(int p_start, int p_goal) {
		return (uint64_t(uint32_t(p_start)) << 32) | uint64_t(uint32_t(p_goal));
	}

public:
	// Look up a cached path; returns true and fills r_path on hit. Bumping the
	// epoch (via check_epoch) clears the cache first.
	bool get(int p_start, int p_goal, PortalPath &r_path) const;
	void put(int p_start, int p_goal, const PortalPath &p_path);

	// If p_epoch differs from the last seen epoch, clear the cache and adopt it.
	// Returns true if the cache was invalidated.
	bool check_epoch(uint64_t p_epoch);

	void clear() { cache.clear(); }
	int size() const { return cache.size(); }
	uint64_t get_epoch() const { return last_epoch; }
};

#endif // PORTAL_GRAPH_H
