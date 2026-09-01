/**************************************************************************/
/*  test_symphony_portal_graph.cpp                                        */
/*  Suite: [Symphony][Spatial][PortalGraph] — graph + Dijkstra (Task 14). */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_portal_graph)

#include "modules/symphony/spatial/portal_graph.h"

namespace TestSymphonyPortalGraph {

// The portal graph is abstract (integer rooms + weighted edges), so the routing
// algorithm is verified directly without any SceneTree.

// --- Same-room short-circuit -------------------------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Same-room path is reachable with no hops") {
	PortalGraph g;
	g.set_node_count(3);
	g.add_edge(0, 1, 1.0f, true);
	g.add_edge(1, 2, 1.0f, true);

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 1, 1);
	CHECK(p.reachable);
	CHECK(p.edge_ids.size() == 0); // no portals traversed
	CHECK(p.total_cost == doctest::Approx(0.0f));
}

// --- Adjacent rooms → single hop ---------------------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Adjacent rooms find the single-hop path") {
	PortalGraph g;
	g.set_node_count(2);
	int e = g.add_edge(0, 1, 2.5f, true);

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 0, 1);
	CHECK(p.reachable);
	REQUIRE(p.edge_ids.size() == 1);
	CHECK(p.edge_ids[0] == e);
	CHECK(p.total_cost == doctest::Approx(2.5f));
}

// --- Three-room chain → two hops ---------------------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Three-room chain finds two hops") {
	PortalGraph g;
	g.set_node_count(3);
	int e0 = g.add_edge(0, 1, 1.0f, true);
	int e1 = g.add_edge(1, 2, 1.0f, true);

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 0, 2);
	CHECK(p.reachable);
	REQUIRE(p.edge_ids.size() == 2);
	CHECK(p.edge_ids[0] == e0);
	CHECK(p.edge_ids[1] == e1);
	CHECK(p.total_cost == doctest::Approx(2.0f));
}

// --- Dijkstra picks the cheaper of two routes --------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Dijkstra picks the lowest-cost route") {
	// 0—1—3 (cost 1+1=2) vs 0—2—3 (cost 5+5=10). Expect the 0-1-3 route.
	PortalGraph g;
	g.set_node_count(4);
	int e01 = g.add_edge(0, 1, 1.0f, true);
	g.add_edge(0, 2, 5.0f, true);
	int e13 = g.add_edge(1, 3, 1.0f, true);
	g.add_edge(2, 3, 5.0f, true);

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 0, 3);
	CHECK(p.reachable);
	REQUIRE(p.edge_ids.size() == 2);
	CHECK(p.edge_ids[0] == e01);
	CHECK(p.edge_ids[1] == e13);
	CHECK(p.total_cost == doctest::Approx(2.0f));
}

// --- Closed portal reroutes --------------------------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Closing a portal reroutes around it") {
	// Square: 0-1 (closed), 0-2, 2-3, 3-1 all open. 0→1 must go the long way.
	PortalGraph g;
	g.set_node_count(4);
	g.add_edge(0, 1, 1.0f, false); // closed direct door
	int e02 = g.add_edge(0, 2, 1.0f, true);
	int e23 = g.add_edge(2, 3, 1.0f, true);
	int e31 = g.add_edge(3, 1, 1.0f, true);

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 0, 1);
	CHECK(p.reachable);
	REQUIRE(p.edge_ids.size() == 3);
	CHECK(p.edge_ids[0] == e02);
	CHECK(p.edge_ids[1] == e23);
	CHECK(p.edge_ids[2] == e31);
	CHECK(p.total_cost == doctest::Approx(3.0f));
}

// --- Closed portal → unreachable ---------------------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Sole closed portal makes goal unreachable") {
	PortalGraph g;
	g.set_node_count(2);
	g.add_edge(0, 1, 1.0f, false); // only link, closed

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 0, 1);
	CHECK_FALSE(p.reachable);
	CHECK(p.edge_ids.size() == 0);
}

TEST_CASE("[Symphony][Spatial][PortalGraph] Disconnected rooms are unreachable") {
	PortalGraph g;
	g.set_node_count(3);
	g.add_edge(0, 1, 1.0f, true);
	// room 2 has no edges

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 0, 2);
	CHECK_FALSE(p.reachable);
}

// --- Path cache ---------------------------------------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Path cache stores and retrieves") {
	PortalPathCache cache;
	cache.check_epoch(1);

	PortalPath p;
	p.reachable = true;
	p.total_cost = 4.0f;
	p.edge_ids.push_back(7);
	cache.put(0, 3, p);

	PortalPath out;
	CHECK(cache.get(0, 3, out));
	CHECK(out.reachable);
	CHECK(out.total_cost == doctest::Approx(4.0f));
	REQUIRE(out.edge_ids.size() == 1);
	CHECK(out.edge_ids[0] == 7);

	// A different key misses.
	PortalPath miss;
	CHECK_FALSE(cache.get(3, 0, miss));
}

TEST_CASE("[Symphony][Spatial][PortalGraph] Cache invalidates on epoch change") {
	PortalPathCache cache;
	cache.check_epoch(1);

	PortalPath p;
	p.reachable = true;
	cache.put(0, 1, p);
	CHECK(cache.size() == 1);

	// Same epoch → no invalidation.
	CHECK_FALSE(cache.check_epoch(1));
	CHECK(cache.size() == 1);

	// Epoch bump (portal opened/closed/moved) → cache cleared.
	CHECK(cache.check_epoch(2));
	CHECK(cache.size() == 0);
	PortalPath out;
	CHECK_FALSE(cache.get(0, 1, out));
}

// --- Scaling: bounded solve over a large graph -------------------------

TEST_CASE("[Symphony][Spatial][PortalGraph] Solve is bounded for a 50-room chain") {
	PortalGraph g;
	g.set_node_count(50);
	for (int i = 0; i < 49; i++) {
		g.add_edge(i, i + 1, 1.0f, true);
	}

	DijkstraPathSolver solver;
	PortalPath p = solver.solve(g, 0, 49);
	CHECK(p.reachable);
	CHECK(p.edge_ids.size() == 49);
	CHECK(p.total_cost == doctest::Approx(49.0f));
}

} // namespace TestSymphonyPortalGraph
