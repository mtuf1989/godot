#include "portal_graph.h"

int PortalGraph::add_edge(int p_a, int p_b, float p_weight, bool p_open, const Vector3 &p_center, int p_portal_id, float p_aperture_area) {
	if (p_a < 0 || p_b < 0 || p_a >= node_count || p_b >= node_count) {
		return -1;
	}
	PortalEdge e;
	e.room_a = p_a;
	e.room_b = p_b;
	e.weight = MAX(p_weight, 0.0f);
	e.open = p_open;
	e.world_center = p_center;
	e.aperture_area = MAX(p_aperture_area, 0.01f);
	e.portal_id = p_portal_id;
	int idx = (int)edges.size();
	edges.push_back(e);
	adjacency[p_a].push_back(idx);
	adjacency[p_b].push_back(idx);
	return idx;
}

PortalPath DijkstraPathSolver::solve(const PortalGraph &p_graph, int p_start_room, int p_goal_room) {
	PortalPath result;

	// Invalid rooms → unreachable.
	if (p_start_room < 0 || p_goal_room < 0 ||
			p_start_room >= p_graph.node_count || p_goal_room >= p_graph.node_count) {
		return result;
	}

	// Same room → trivially reachable with no hops (short-circuit; no solve).
	if (p_start_room == p_goal_room) {
		result.reachable = true;
		return result;
	}

	const int n = p_graph.node_count;
	LocalVector<float> dist;
	LocalVector<int> prev_edge; // edge used to reach each node
	LocalVector<int> prev_node;
	LocalVector<bool> visited;
	dist.resize(n);
	prev_edge.resize(n);
	prev_node.resize(n);
	visited.resize(n);
	for (int i = 0; i < n; i++) {
		dist[i] = INFINITY;
		prev_edge[i] = -1;
		prev_node[i] = -1;
		visited[i] = false;
	}
	dist[p_start_room] = 0.0f;

	// Simple O(V²) Dijkstra — room counts are small (tens), and this avoids
	// pulling in a heap. Bounded and allocation-light.
	for (int iter = 0; iter < n; iter++) {
		int u = -1;
		float best = INFINITY;
		for (int i = 0; i < n; i++) {
			if (!visited[i] && dist[i] < best) {
				best = dist[i];
				u = i;
			}
		}
		if (u < 0) {
			break; // remaining nodes unreachable
		}
		if (u == p_goal_room) {
			break; // reached the goal
		}
		visited[u] = true;

		const LocalVector<int> &adj = p_graph.adjacency[u];
		for (uint32_t k = 0; k < adj.size(); k++) {
			const PortalEdge &e = p_graph.edges[adj[k]];
			if (!e.open) {
				continue; // closed portal blocks this edge
			}
			int v = e.other(u);
			if (v < 0 || visited[v]) {
				continue;
			}
			float nd = dist[u] + e.weight;
			if (nd < dist[v]) {
				dist[v] = nd;
				prev_edge[v] = adj[k];
				prev_node[v] = u;
			}
		}
	}

	if (Math::is_inf(dist[p_goal_room])) {
		return result; // unreachable
	}

	// Reconstruct edge path goal → start, then reverse.
	result.reachable = true;
	result.total_cost = dist[p_goal_room];
	LocalVector<int> rev;
	int cur = p_goal_room;
	while (cur != p_start_room && prev_edge[cur] >= 0) {
		rev.push_back(prev_edge[cur]);
		cur = prev_node[cur];
	}
	for (int i = (int)rev.size() - 1; i >= 0; i--) {
		result.edge_ids.push_back(rev[i]);
	}
	return result;
}

bool PortalPathCache::get(int p_start, int p_goal, PortalPath &r_path) const {
	const Entry *found = cache.getptr(_key(p_start, p_goal));
	if (found) {
		r_path = found->path;
		// Touch LRU tick (get is const; cast to update bookkeeping only).
		PortalPathCache *self = const_cast<PortalPathCache *>(this);
		self->use_tick++;
		self->cache[_key(p_start, p_goal)].last_use = self->use_tick;
		return true;
	}
	return false;
}

void PortalPathCache::put(int p_start, int p_goal, const PortalPath &p_path) {
	const uint64_t key = _key(p_start, p_goal);
	use_tick++;
	if (!cache.has(key) && (int)cache.size() >= MAX_ENTRIES) {
		// Evict the least-recently-used entry.
		uint64_t oldest_key = 0;
		uint64_t oldest_use = UINT64_MAX;
		bool found = false;
		for (const KeyValue<uint64_t, Entry> &kv : cache) {
			if (kv.value.last_use < oldest_use) {
				oldest_use = kv.value.last_use;
				oldest_key = kv.key;
				found = true;
			}
		}
		if (found) {
			cache.erase(oldest_key);
		}
	}
	Entry e;
	e.path = p_path;
	e.last_use = use_tick;
	cache[key] = e;
}

bool PortalPathCache::check_epoch(uint64_t p_epoch) {
	if (p_epoch != last_epoch) {
		cache.clear();
		last_epoch = p_epoch;
		return true;
	}
	return false;
}
