#include "acoustic_body_3d.h"

#include "core/object/class_db.h"
#include "scene/3d/physics/collision_object_3d.h"

// Try to include CSGShape3D if the module is available.
// CSG shapes create an internal StaticBody3D when use_collision is enabled;
// we register both the CSG parent's ID and the internal body's ID.
#ifdef MODULE_CSG_ENABLED
#include "modules/csg/csg_shape.h"
#endif

// Static registry definition.
HashMap<ObjectID, AcousticMaterial *> AcousticBody3D::registry;

AcousticBody3D::AcousticBody3D() {
}

AcousticBody3D::~AcousticBody3D() {
	// Safety: ensure no dangling entries if the destructor runs without exit_tree.
	_unregister();
}

void AcousticBody3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_register();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_unregister();
		} break;
	}
}

void AcousticBody3D::_register() {
	_unregister(); // Clear any stale entries first.

	if (material.is_null()) {
		return;
	}

	Node *parent = get_parent();
	if (parent == nullptr) {
		return;
	}

	AcousticMaterial *mat_ptr = material.ptr();

	// Case 1: Parent is a CollisionObject3D (StaticBody3D, RigidBody3D, Area3D, CharacterBody3D).
	CollisionObject3D *collision_parent = Object::cast_to<CollisionObject3D>(parent);
	if (collision_parent) {
		ObjectID id = collision_parent->get_instance_id();
		registry[id] = mat_ptr;
		registered_ids.push_back(id);
		return;
	}

#ifdef MODULE_CSG_ENABLED
	// Case 2: Parent is a CSGShape3D with use_collision enabled.
	// CSG creates an internal StaticBody3D that raycasts hit.
	CSGShape3D *csg_parent = Object::cast_to<CSGShape3D>(parent);
	if (csg_parent) {
		// Register the CSG node itself.
		ObjectID csg_id = csg_parent->get_instance_id();
		registry[csg_id] = mat_ptr;
		registered_ids.push_back(csg_id);

		// Also try to find the internal collision body.
		// CSG shapes with use_collision create a child StaticBody3D.
		for (int i = 0; i < csg_parent->get_child_count(); i++) {
			CollisionObject3D *internal_body = Object::cast_to<CollisionObject3D>(csg_parent->get_child(i));
			if (internal_body) {
				ObjectID body_id = internal_body->get_instance_id();
				registry[body_id] = mat_ptr;
				registered_ids.push_back(body_id);
				break; // Only one internal body expected.
			}
		}
	}
#endif
}

void AcousticBody3D::_unregister() {
	for (int i = 0; i < registered_ids.size(); i++) {
		registry.erase(registered_ids[i]);
	}
	registered_ids.clear();
}

void AcousticBody3D::set_material(const Ref<AcousticMaterial> &p_material) {
	if (material == p_material) {
		return;
	}
	material = p_material;

	// Re-register with new material if already in tree.
	if (is_inside_tree()) {
		_unregister();
		_register();
	}
}

Ref<AcousticMaterial> AcousticBody3D::get_material() const {
	return material;
}

AcousticMaterial *AcousticBody3D::lookup_material(ObjectID p_collider_id) {
	AcousticMaterial **found = registry.getptr(p_collider_id);
	if (found) {
		return *found;
	}
	return nullptr;
}

bool AcousticBody3D::has_material(ObjectID p_collider_id) {
	return registry.has(p_collider_id);
}

int AcousticBody3D::get_registry_size() {
	return registry.size();
}

void AcousticBody3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_material", "material"), &AcousticBody3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &AcousticBody3D::get_material);

	ClassDB::bind_static_method("AcousticBody3D", D_METHOD("lookup_material", "collider_id"), &AcousticBody3D::lookup_material);
	ClassDB::bind_static_method("AcousticBody3D", D_METHOD("has_material", "collider_id"), &AcousticBody3D::has_material);
	ClassDB::bind_static_method("AcousticBody3D", D_METHOD("get_registry_size"), &AcousticBody3D::get_registry_size);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "AcousticMaterial"), "set_material", "get_material");
}
