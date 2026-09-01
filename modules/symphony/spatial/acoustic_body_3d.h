#ifndef ACOUSTIC_BODY_3D_H
#define ACOUSTIC_BODY_3D_H

#include "scene/3d/node_3d.h"
#include "acoustic_material.h"
#include "core/templates/hash_map.h"
#include "core/object/object_id.h"

// Attach as a child of any CollisionObject3D or CSGShape3D to assign acoustic
// properties to that surface. Registers the parent's ObjectID in a static
// HashMap for O(1) lookup from raycast hits.
//
// Replaces the addon's find_for_collider() tree-walk with a direct map lookup.
class AcousticBody3D : public Node3D {
	GDCLASS(AcousticBody3D, Node3D);

private:
	Ref<AcousticMaterial> material;

	// Static registry: parent CollisionObject3D ObjectID → AcousticMaterial pointer.
	// Written on main thread only (enter/exit tree). Read on main thread (raycast results).
	static HashMap<ObjectID, AcousticMaterial *> registry;

	// Tracks which ObjectIDs this instance registered so we clean up correctly.
	Vector<ObjectID> registered_ids;

	void _register();
	void _unregister();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_material(const Ref<AcousticMaterial> &p_material);
	Ref<AcousticMaterial> get_material() const;

	// O(1) lookup: given a collider ObjectID from a raycast result, return
	// the AcousticMaterial or nullptr if no AcousticBody3D covers it.
	static AcousticMaterial *lookup_material(ObjectID p_collider_id);

	// Check if a collider is registered.
	static bool has_material(ObjectID p_collider_id);

	// Get registry size (debug/test).
	static int get_registry_size();

	AcousticBody3D();
	~AcousticBody3D();
};

#endif // ACOUSTIC_BODY_3D_H
