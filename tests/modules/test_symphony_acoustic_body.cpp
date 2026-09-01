/**************************************************************************/
/*  test_symphony_acoustic_body.cpp                                       */
/*  Suite: [Symphony][Spatial][AcousticBody] — collider→material registry.*/
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_acoustic_body)

#include "modules/symphony/spatial/acoustic_body_3d.h"
#include "modules/symphony/spatial/acoustic_material.h"

namespace TestSymphonyAcousticBody {

// AcousticBody3D is a Node3D (headless-safe to construct/destruct), but its
// REGISTRATION path requires a CollisionObject3D parent inside a live SceneTree
// with the 3D physics server stood up — which the headless doctest harness does
// not provide (no test constructs a StaticBody3D/Area3D for the same reason).
// So here we verify:
//   • the static registry LOOKUP contract that OcclusionSolver/RoomEstimator
//     actually call (lookup_material / has_material on collider ids), and
//   • the material property + no-crash lifecycle of a bare node.
// Full in-tree registration + nearest-ancestor resolution is exercised by the
// live-SceneTree GdUnit4 integration tests in game-template.

// --- Registry lookup contract ------------------------------------------

TEST_CASE("[Symphony][Spatial][AcousticBody] Unknown collider id resolves to null") {
	// An id that was never registered → no material.
	ObjectID bogus = ObjectID((uint64_t)0xDEADBEEF);
	CHECK(AcousticBody3D::lookup_material(bogus) == nullptr);
	CHECK_FALSE(AcousticBody3D::has_material(bogus));
}

TEST_CASE("[Symphony][Spatial][AcousticBody] Invalid (zero) collider id resolves to null") {
	ObjectID invalid; // default = invalid
	CHECK(AcousticBody3D::lookup_material(invalid) == nullptr);
	CHECK_FALSE(AcousticBody3D::has_material(invalid));
}

TEST_CASE("[Symphony][Spatial][AcousticBody] Registry size is a stable non-negative baseline") {
	// No bodies are registered in the headless harness (no in-tree CollisionObject3D),
	// so the size query is well-defined and does not crash.
	int size = AcousticBody3D::get_registry_size();
	CHECK(size >= 0);
}

// --- Node material property (headless-safe, not in tree) ---------------

TEST_CASE("[Symphony][Spatial][AcousticBody] Material property round-trips on a bare node") {
	AcousticBody3D *body = memnew(AcousticBody3D);
	CHECK(body->get_material().is_null()); // none by default

	Ref<AcousticMaterial> mat = AcousticMaterial::create_preset(AcousticMaterial::PRESET_BRICK);
	body->set_material(mat);
	CHECK(body->get_material() == mat);

	// Not in the tree → nothing was registered (registration is ENTER_TREE-gated).
	CHECK_FALSE(AcousticBody3D::has_material(body->get_instance_id()));

	memdelete(body);
}

TEST_CASE("[Symphony][Spatial][AcousticBody] Freeing a never-registered body leaves the registry clean") {
	int before = AcousticBody3D::get_registry_size();
	AcousticBody3D *body = memnew(AcousticBody3D);
	body->set_material(AcousticMaterial::create_preset(AcousticMaterial::PRESET_WOOD));
	memdelete(body); // dtor calls _unregister(); must be a safe no-op here
	int after = AcousticBody3D::get_registry_size();
	CHECK(after == before);
}

} // namespace TestSymphonyAcousticBody
