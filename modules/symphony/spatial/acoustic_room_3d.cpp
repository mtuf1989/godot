#include "acoustic_room_3d.h"

#include "core/object/class_db.h"

LocalVector<AcousticRoom3D *> AcousticRoom3D::rooms;
uint64_t AcousticRoom3D::registry_epoch = 1;

AcousticRoom3D::AcousticRoom3D() {
}

AcousticRoom3D::~AcousticRoom3D() {
	_unregister();
}

void AcousticRoom3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_register();
			set_notify_transform(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_notify_transform(false);
			_unregister();
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			// Movement invalidates any cached membership resolved against old bounds.
			registry_epoch++;
		} break;
	}
}

void AcousticRoom3D::_register() {
	// Avoid duplicate registration.
	for (uint32_t i = 0; i < rooms.size(); i++) {
		if (rooms[i] == this) {
			return;
		}
	}
	rooms.push_back(this);
	registry_epoch++;
}

void AcousticRoom3D::_unregister() {
	for (uint32_t i = 0; i < rooms.size(); i++) {
		if (rooms[i] == this) {
			rooms.remove_at(i);
			registry_epoch++;
			return;
		}
	}
}

void AcousticRoom3D::set_bounds(const Vector3 &p_bounds) {
	bounds = Vector3(MAX(p_bounds.x, 0.01f), MAX(p_bounds.y, 0.01f), MAX(p_bounds.z, 0.01f));
	registry_epoch++;
	if (is_inside_tree()) {
		update_gizmos();
	}
}

void AcousticRoom3D::set_shoebox_dimensions(const Vector3 &p_dims) {
	shoebox_dimensions = Vector3(MAX(p_dims.x, 0.0f), MAX(p_dims.y, 0.0f), MAX(p_dims.z, 0.0f));
}

void AcousticRoom3D::set_room_priority(int p_priority) {
	priority = p_priority;
	registry_epoch++;
}

bool AcousticRoom3D::point_in_box(const Transform3D &p_room_xform, const Vector3 &p_half_extents, const Vector3 &p_world_point) {
	const Vector3 local = p_room_xform.affine_inverse().xform(p_world_point);
	return Math::abs(local.x) <= p_half_extents.x &&
			Math::abs(local.y) <= p_half_extents.y &&
			Math::abs(local.z) <= p_half_extents.z;
}

bool AcousticRoom3D::contains_point(const Vector3 &p_world_point) const {
	// Transform the world point into the room's local space and test against the
	// axis-aligned half-extent box. Using the inverse global transform handles
	// rotation/scale so a rotated room is an oriented bounding box in world space.
	if (!is_inside_tree()) {
		return false;
	}
	return point_in_box(get_global_transform(), bounds, p_world_point);
}

AcousticRoom3D *AcousticRoom3D::find_room_for_point(const Vector3 &p_world_point) {
	AcousticRoom3D *best = nullptr;
	int best_priority = 0;
	for (uint32_t i = 0; i < rooms.size(); i++) {
		AcousticRoom3D *room = rooms[i];
		if (room == nullptr || !room->contains_point(p_world_point)) {
			continue;
		}
		if (best == nullptr || room->priority > best_priority) {
			best = room;
			best_priority = room->priority;
		}
	}
	return best;
}

AcousticRoom3D *AcousticRoom3D::get_room(int p_index) {
	if (p_index < 0 || p_index >= (int)rooms.size()) {
		return nullptr;
	}
	return rooms[p_index];
}

void AcousticRoom3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_bounds", "bounds"), &AcousticRoom3D::set_bounds);
	ClassDB::bind_method(D_METHOD("get_bounds"), &AcousticRoom3D::get_bounds);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &AcousticRoom3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &AcousticRoom3D::get_material);
	ClassDB::bind_method(D_METHOD("set_shoebox_dimensions", "dimensions"), &AcousticRoom3D::set_shoebox_dimensions);
	ClassDB::bind_method(D_METHOD("get_shoebox_dimensions"), &AcousticRoom3D::get_shoebox_dimensions);
	ClassDB::bind_method(D_METHOD("has_authored_shoebox"), &AcousticRoom3D::has_authored_shoebox);
	ClassDB::bind_method(D_METHOD("set_reverb_preset_override", "material"), &AcousticRoom3D::set_reverb_preset_override);
	ClassDB::bind_method(D_METHOD("get_reverb_preset_override"), &AcousticRoom3D::get_reverb_preset_override);
	ClassDB::bind_method(D_METHOD("set_room_priority", "priority"), &AcousticRoom3D::set_room_priority);
	ClassDB::bind_method(D_METHOD("get_room_priority"), &AcousticRoom3D::get_room_priority);

	ClassDB::bind_method(D_METHOD("contains_point", "world_point"), &AcousticRoom3D::contains_point);

	ClassDB::bind_static_method("AcousticRoom3D", D_METHOD("find_room_for_point", "world_point"), &AcousticRoom3D::find_room_for_point);
	ClassDB::bind_static_method("AcousticRoom3D", D_METHOD("get_room_count"), &AcousticRoom3D::get_room_count);
	ClassDB::bind_static_method("AcousticRoom3D", D_METHOD("get_room", "index"), &AcousticRoom3D::get_room);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "bounds", PROPERTY_HINT_NONE, "suffix:m"), "set_bounds", "get_bounds");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "AcousticMaterial"), "set_material", "get_material");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "shoebox_dimensions", PROPERTY_HINT_NONE, "suffix:m"), "set_shoebox_dimensions", "get_shoebox_dimensions");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "reverb_preset_override", PROPERTY_HINT_RESOURCE_TYPE, "AcousticMaterial"), "set_reverb_preset_override", "get_reverb_preset_override");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "room_priority"), "set_room_priority", "get_room_priority");
}
