/**************************************************************************/
/*  test_symphony_portal_router.cpp                                       */
/*  Suite: [Symphony][Spatial][PortalRouter] — routing/diffraction (T15). */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_portal_router)

#include "modules/symphony/spatial/portal_router.h"

namespace TestSymphonyPortalRouter {

static PortalHop make_hop(const Vector3 &c, const Vector3 &n, float area) {
	PortalHop h;
	h.center = c;
	h.normal = n;
	h.aperture_area = area;
	return h;
}

// --- Apparent position --------------------------------------------------

TEST_CASE("[Symphony][Spatial][PortalRouter] Same-room keeps true position") {
	LocalVector<PortalHop> hops; // none
	Vector3 src(10, 0, 0), lis(0, 0, 0);
	CHECK(PortalRouter::apparent_position(src, lis, hops).is_equal_approx(src));
}

TEST_CASE("[Symphony][Spatial][PortalRouter] One hop → apparent position at the portal") {
	LocalVector<PortalHop> hops;
	hops.push_back(make_hop(Vector3(0, 0, 5), Vector3(0, 0, 1), 2.0f));
	Vector3 src(0, 0, 20), lis(0, 0, 0);
	Vector3 ap = PortalRouter::apparent_position(src, lis, hops);
	CHECK(ap.is_equal_approx(Vector3(0, 0, 5))); // the doorway, not the true source
}

TEST_CASE("[Symphony][Spatial][PortalRouter] Two hops → apparent position at the last portal") {
	LocalVector<PortalHop> hops;
	hops.push_back(make_hop(Vector3(0, 0, 10), Vector3(0, 0, 1), 2.0f));
	hops.push_back(make_hop(Vector3(0, 0, 5), Vector3(0, 0, 1), 2.0f)); // nearest listener
	Vector3 ap = PortalRouter::apparent_position(Vector3(0, 0, 20), Vector3(0, 0, 0), hops);
	CHECK(ap.is_equal_approx(Vector3(0, 0, 5)));
}

// --- Deviation + diffraction cutoff ------------------------------------

TEST_CASE("[Symphony][Spatial][PortalRouter] Straight path has zero deviation, full bandwidth") {
	LocalVector<PortalHop> hops;
	// Portal centre lies exactly on the straight line source→listener.
	hops.push_back(make_hop(Vector3(0, 0, 5), Vector3(0, 0, 1), 2.0f));
	Vector3 src(0, 0, 10), lis(0, 0, 0);
	CHECK(PortalRouter::total_deviation(src, lis, hops) == doctest::Approx(0.0f).epsilon(0.01));
	CHECK(PortalRouter::diffraction_cutoff(src, lis, hops, 20000.0f, 700.0f) == doctest::Approx(20000.0f).epsilon(0.01));
}

TEST_CASE("[Symphony][Spatial][PortalRouter] 90-degree bend attenuates highs") {
	LocalVector<PortalHop> hops;
	// Source in front (+Z), doorway at origin-ish, listener off to the side (+X):
	// the path bends ~90° at the portal.
	hops.push_back(make_hop(Vector3(0, 0, 0), Vector3(0, 0, 1), 2.0f));
	Vector3 src(0, 0, 5), lis(5, 0, 0);
	float dev = PortalRouter::total_deviation(src, lis, hops);
	CHECK(dev == doctest::Approx(Math::PI * 0.5f).epsilon(0.02)); // ~90°

	float straight_cut = 20000.0f;
	float bent_cut = PortalRouter::diffraction_cutoff(src, lis, hops, 20000.0f, 700.0f);
	CHECK(bent_cut < straight_cut); // highs rolled off
	// At ~90° (half of π) the cutoff should be roughly the midpoint.
	CHECK(bent_cut == doctest::Approx(Math::lerp(20000.0f, 700.0f, 0.5f)).epsilon(0.05));
}

TEST_CASE("[Symphony][Spatial][PortalRouter] More deviation → lower cutoff (monotonic)") {
	Vector3 src(0, 0, 5);
	LocalVector<PortalHop> hops;
	hops.push_back(make_hop(Vector3(0, 0, 0), Vector3(0, 0, 1), 2.0f));

	// Listener slightly off-axis vs sharply off-axis.
	float cut_small = PortalRouter::diffraction_cutoff(src, Vector3(1, 0, -2), hops, 20000.0f, 700.0f);
	float cut_large = PortalRouter::diffraction_cutoff(src, Vector3(5, 0, 0), hops, 20000.0f, 700.0f);
	CHECK(cut_large < cut_small);
}

// --- Path gain: aperture + incidence -----------------------------------

TEST_CASE("[Symphony][Spatial][PortalRouter] No hops → unity gain") {
	LocalVector<PortalHop> hops;
	CHECK(PortalRouter::path_gain(Vector3(1, 0, 0), Vector3(0, 0, 0), hops) == doctest::Approx(1.0f));
}

TEST_CASE("[Symphony][Spatial][PortalRouter] Larger aperture passes more energy") {
	Vector3 src(0, 0, 10), lis(0, 0, 0);
	LocalVector<PortalHop> small_ap, big_ap;
	small_ap.push_back(make_hop(Vector3(0, 0, 5), Vector3(0, 0, 1), 0.5f));
	big_ap.push_back(make_hop(Vector3(0, 0, 5), Vector3(0, 0, 1), 2.0f));

	float g_small = PortalRouter::path_gain(src, lis, small_ap, 2.0f);
	float g_big = PortalRouter::path_gain(src, lis, big_ap, 2.0f);
	CHECK(g_big > g_small);
	CHECK(g_big == doctest::Approx(1.0f).epsilon(0.01)); // area >= ref, straight-on
}

TEST_CASE("[Symphony][Spatial][PortalRouter] Oblique incidence attenuates more than head-on") {
	Vector3 src(0, 0, 10), lis(0, 0, 0);
	// Head-on: path crosses portal along its normal (+Z).
	LocalVector<PortalHop> head_on;
	head_on.push_back(make_hop(Vector3(0, 0, 5), Vector3(0, 0, 1), 2.0f));
	float g_head = PortalRouter::path_gain(src, lis, head_on, 2.0f);

	// Oblique: same aperture, but normal tilted 60° from the traversal direction.
	LocalVector<PortalHop> oblique;
	Vector3 tilted = Vector3(Math::sin(Math::deg_to_rad(60.0f)), 0, Math::cos(Math::deg_to_rad(60.0f)));
	oblique.push_back(make_hop(Vector3(0, 0, 5), tilted, 2.0f));
	float g_obl = PortalRouter::path_gain(src, lis, oblique, 2.0f);

	CHECK(g_obl < g_head);
}

} // namespace TestSymphonyPortalRouter
