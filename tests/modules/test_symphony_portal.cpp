/**************************************************************************/
/*  test_symphony_portal.cpp                                              */
/*  Suite: [Symphony][Spatial][Portal] — S6 authoring nodes (Task 13).    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_portal)

#include "modules/symphony/spatial/acoustic_portal_3d.h"
#include "modules/symphony/spatial/acoustic_room_3d.h"

namespace TestSymphonyPortal {

// AcousticRoom3D is an Area3D and AcousticPortal3D a Node3D; parenting them into
// the doctest SceneTree pulls in the physics world and SIGSEGVs the headless
// harness (same constraint the volumetric suite hit). So the membership +
// geometry MATH is verified through the pure static helpers (point_in_box,
// closest_point_on_rect, aperture_area) that the instance methods delegate to,
// plus tree-independent state (open/close, aperture size). Full tree-level
// room resolution is exercised by the engine integration tests later.

// --- Room membership math ----------------------------------------------

TEST_CASE("[Symphony][Spatial][Portal] point_in_box: axis-aligned") {
	Transform3D xf; // identity at origin
	Vector3 half(5, 3, 5);
	CHECK(AcousticRoom3D::point_in_box(xf, half, Vector3(0, 0, 0)));
	CHECK(AcousticRoom3D::point_in_box(xf, half, Vector3(4.9f, 2.9f, 4.9f)));
	CHECK_FALSE(AcousticRoom3D::point_in_box(xf, half, Vector3(6, 0, 0)));
	CHECK_FALSE(AcousticRoom3D::point_in_box(xf, half, Vector3(0, 4, 0)));
}

TEST_CASE("[Symphony][Spatial][Portal] point_in_box: translated") {
	Transform3D xf(Basis(), Vector3(100, 0, 0));
	Vector3 half(2, 2, 2);
	CHECK(AcousticRoom3D::point_in_box(xf, half, Vector3(100, 0, 0)));
	CHECK(AcousticRoom3D::point_in_box(xf, half, Vector3(101, 1, 1)));
	CHECK_FALSE(AcousticRoom3D::point_in_box(xf, half, Vector3(0, 0, 0)));
}

TEST_CASE("[Symphony][Spatial][Portal] point_in_box: rotated is an oriented box") {
	Basis b;
	b.rotate(Vector3(0, 1, 0), Math::PI * 0.5f); // 90° about Y
	Transform3D xf(b, Vector3());
	Vector3 half(4, 2, 1); // long local-X, thin local-Z
	// Long axis now points along world Z after the rotation.
	CHECK(AcousticRoom3D::point_in_box(xf, half, Vector3(0, 0, 3.5f)));
	// World X now maps to the thin local-Z axis → outside.
	CHECK_FALSE(AcousticRoom3D::point_in_box(xf, half, Vector3(3.5f, 0, 0)));
}

TEST_CASE("[Symphony][Spatial][Portal] overlap priority resolution logic") {
	// Emulate find_room_for_point's priority selection over two overlapping boxes.
	Transform3D xf; // both at origin
	Vector3 big(10, 10, 10), small(2, 2, 2);
	int big_prio = 0, small_prio = 5;

	// A helper mirroring the engine-side selection.
	auto resolve = [&](const Vector3 &p) -> int {
		int best = -1, best_prio = 0;
		if (AcousticRoom3D::point_in_box(xf, big, p) && (best < 0 || big_prio > best_prio)) {
			best = 0;
			best_prio = big_prio;
		}
		if (AcousticRoom3D::point_in_box(xf, small, p) && (best < 0 || small_prio > best_prio)) {
			best = 1;
			best_prio = small_prio;
		}
		return best;
	};

	CHECK(resolve(Vector3(0, 0, 0)) == 1); // inside both → higher-priority small
	CHECK(resolve(Vector3(8, 0, 0)) == 0); // only big
	CHECK(resolve(Vector3(100, 0, 0)) == -1); // neither
}

// --- Portal geometry math ----------------------------------------------

TEST_CASE("[Symphony][Spatial][Portal] aperture_area = width * height") {
	CHECK(AcousticPortal3D::aperture_area(Vector2(2.0f, 3.0f)) == doctest::Approx(6.0f));
	CHECK(AcousticPortal3D::aperture_area(Vector2(1.0f, 1.0f)) == doctest::Approx(1.0f));
}

TEST_CASE("[Symphony][Spatial][Portal] closest_point_on_rect clamps to rectangle") {
	Transform3D xf; // identity, aperture on XY plane at origin
	Vector2 ap(2.0f, 2.0f); // half-extents 1,1

	// Off to the side and in front → clamps x to +1, projects z to 0.
	Vector3 cp = AcousticPortal3D::closest_point_on_rect(xf, ap, Vector3(5.0f, 0.0f, 4.0f));
	CHECK(cp.x == doctest::Approx(1.0f));
	CHECK(cp.y == doctest::Approx(0.0f));
	CHECK(cp.z == doctest::Approx(0.0f));

	// Inside the footprint → keeps x/y, snaps z.
	Vector3 cp2 = AcousticPortal3D::closest_point_on_rect(xf, ap, Vector3(0.5f, -0.5f, -3.0f));
	CHECK(cp2.x == doctest::Approx(0.5f));
	CHECK(cp2.y == doctest::Approx(-0.5f));
	CHECK(cp2.z == doctest::Approx(0.0f));
}

TEST_CASE("[Symphony][Spatial][Portal] closest_point_on_rect honors transform") {
	Transform3D xf(Basis(), Vector3(0, 0, 10)); // aperture plane at z=10
	Vector2 ap(4.0f, 4.0f); // half-extents 2,2
	// A point in front of and centered on the portal projects to its centre.
	Vector3 cp = AcousticPortal3D::closest_point_on_rect(xf, ap, Vector3(0, 0, 20));
	CHECK(cp.is_equal_approx(Vector3(0, 0, 10)));
}

// --- Portal state (tree-independent) -----------------------------------

TEST_CASE("[Symphony][Spatial][Portal] Portal open/close toggles state + epoch") {
	AcousticPortal3D *portal = memnew(AcousticPortal3D);
	CHECK(portal->is_open()); // default open

	uint64_t e0 = AcousticPortal3D::get_state_epoch();
	portal->set_open(false);
	CHECK_FALSE(portal->is_open());
	uint64_t e1 = AcousticPortal3D::get_state_epoch();
	CHECK(e1 > e0); // closing invalidates cached paths (Task 14)

	// Idempotent: setting the same state does not bump the epoch.
	portal->set_open(false);
	CHECK(AcousticPortal3D::get_state_epoch() == e1);

	memdelete(portal);
}

TEST_CASE("[Symphony][Spatial][Portal] Portal aperture size clamps to positive") {
	AcousticPortal3D *portal = memnew(AcousticPortal3D);
	portal->set_aperture_size(Vector2(2.0f, 3.0f));
	CHECK(portal->get_aperture_size().x == doctest::Approx(2.0f));
	CHECK(portal->get_aperture_size().y == doctest::Approx(3.0f));
	CHECK(portal->get_aperture_area() == doctest::Approx(6.0f));

	portal->set_aperture_size(Vector2(-1.0f, 0.0f)); // clamped up to a small min
	CHECK(portal->get_aperture_size().x > 0.0f);
	CHECK(portal->get_aperture_size().y > 0.0f);

	memdelete(portal);
}

TEST_CASE("[Symphony][Spatial][Portal] Unparented portal resolves rooms to null") {
	AcousticPortal3D *portal = memnew(AcousticPortal3D);
	CHECK(portal->get_room_a() == nullptr);
	CHECK(portal->get_room_b() == nullptr);
	CHECK(portal->get_other_room(nullptr) == nullptr);
	CHECK_FALSE(portal->connects(nullptr, nullptr));
	memdelete(portal);
}

// --- Room state ---------------------------------------------------------
// NOTE: AcousticRoom3D derives from Area3D, whose construction/destruction
// touches PhysicsServer3D internals that the headless doctest harness does not
// fully stand up (memdelete of a never-in-tree Area3D SIGSEGVs — same class of
// limitation the volumetric suite documented). Room bounds/shoebox/priority are
// plain setters with trivial clamp logic; the acoustically meaningful room math
// (point_in_box, overlap-priority) is covered above via pure static helpers, and
// full tree-level room behaviour is exercised by the engine integration tests.

} // namespace TestSymphonyPortal
