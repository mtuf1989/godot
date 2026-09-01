#include "acoustic_portal_3d.h"
#include "acoustic_room_3d.h"

#include "core/object/class_db.h"

LocalVector<AcousticPortal3D *> AcousticPortal3D::portals;
uint64_t AcousticPortal3D::state_epoch = 1;
uint64_t AcousticPortal3D::transform_epoch = 1;

AcousticPortal3D::AcousticPortal3D() {
}

AcousticPortal3D::~AcousticPortal3D() {
	_unregister();
}

void AcousticPortal3D::_notification(int p_what) {
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
			// Movement only (Phase 5.3): a portal parented to a swinging door
			// must NOT rebuild the graph / flush the path cache every frame.
			transform_epoch++;
		} break;
	}
}

void AcousticPortal3D::_register() {
	for (uint32_t i = 0; i < portals.size(); i++) {
		if (portals[i] == this) {
			return;
		}
	}
	portals.push_back(this);
	state_epoch++;
}

void AcousticPortal3D::_unregister() {
	for (uint32_t i = 0; i < portals.size(); i++) {
		if (portals[i] == this) {
			portals.remove_at(i);
			state_epoch++;
			return;
		}
	}
}

void AcousticPortal3D::set_aperture_size(const Vector2 &p_size) {
	aperture_size = Vector2(MAX(p_size.x, 0.01f), MAX(p_size.y, 0.01f));
	state_epoch++;
	if (is_inside_tree()) {
		update_gizmos();
	}
}

void AcousticPortal3D::set_room_a_path(const NodePath &p_path) {
	room_a_path = p_path;
	state_epoch++;
}

void AcousticPortal3D::set_room_b_path(const NodePath &p_path) {
	room_b_path = p_path;
	state_epoch++;
}

void AcousticPortal3D::set_open(bool p_open) {
	if (open == p_open) {
		return;
	}
	open = p_open;
	state_epoch++; // portal state change invalidates cached paths (Task 14)
}

AcousticRoom3D *AcousticPortal3D::get_room_a() const {
	if (!is_inside_tree() || room_a_path.is_empty()) {
		return nullptr;
	}
	return Object::cast_to<AcousticRoom3D>(get_node_or_null(room_a_path));
}

AcousticRoom3D *AcousticPortal3D::get_room_b() const {
	if (!is_inside_tree() || room_b_path.is_empty()) {
		return nullptr;
	}
	return Object::cast_to<AcousticRoom3D>(get_node_or_null(room_b_path));
}

AcousticRoom3D *AcousticPortal3D::get_other_room(const AcousticRoom3D *p_room) const {
	AcousticRoom3D *a = get_room_a();
	AcousticRoom3D *b = get_room_b();
	if (p_room == a) {
		return b;
	}
	if (p_room == b) {
		return a;
	}
	return nullptr;
}

bool AcousticPortal3D::connects(const AcousticRoom3D *p_a, const AcousticRoom3D *p_b) const {
	if (p_a == nullptr || p_b == nullptr) {
		return false;
	}
	AcousticRoom3D *a = get_room_a();
	AcousticRoom3D *b = get_room_b();
	return (a == p_a && b == p_b) || (a == p_b && b == p_a);
}

Vector3 AcousticPortal3D::get_world_center() const {
	if (!is_inside_tree()) {
		return Vector3();
	}
	return get_global_transform().origin;
}

Vector3 AcousticPortal3D::get_world_normal() const {
	if (!is_inside_tree()) {
		return Vector3(0, 0, 1);
	}
	// Local +Z axis in world space.
	return get_global_transform().basis.get_column(2).normalized();
}

float AcousticPortal3D::get_aperture_area() const {
	return aperture_size.x * aperture_size.y;
}

Vector3 AcousticPortal3D::closest_point_on_rect(const Transform3D &p_portal_xform, const Vector2 &p_aperture_size, const Vector3 &p_world_point) {
	Vector3 local = p_portal_xform.affine_inverse().xform(p_world_point);
	const float hw = p_aperture_size.x * 0.5f;
	const float hh = p_aperture_size.y * 0.5f;
	local.x = CLAMP(local.x, -hw, hw);
	local.y = CLAMP(local.y, -hh, hh);
	local.z = 0.0f;
	return p_portal_xform.xform(local);
}

Vector3 AcousticPortal3D::closest_point_on_aperture(const Vector3 &p_world_point) const {
	if (!is_inside_tree()) {
		return Vector3();
	}
	return closest_point_on_rect(get_global_transform(), aperture_size, p_world_point);
}

AcousticPortal3D *AcousticPortal3D::get_portal(int p_index) {
	if (p_index < 0 || p_index >= (int)portals.size()) {
		return nullptr;
	}
	return portals[p_index];
}

void AcousticPortal3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_aperture_size", "size"), &AcousticPortal3D::set_aperture_size);
	ClassDB::bind_method(D_METHOD("get_aperture_size"), &AcousticPortal3D::get_aperture_size);
	ClassDB::bind_method(D_METHOD("set_room_a_path", "path"), &AcousticPortal3D::set_room_a_path);
	ClassDB::bind_method(D_METHOD("get_room_a_path"), &AcousticPortal3D::get_room_a_path);
	ClassDB::bind_method(D_METHOD("set_room_b_path", "path"), &AcousticPortal3D::set_room_b_path);
	ClassDB::bind_method(D_METHOD("get_room_b_path"), &AcousticPortal3D::get_room_b_path);
	ClassDB::bind_method(D_METHOD("set_open", "open"), &AcousticPortal3D::set_open);
	ClassDB::bind_method(D_METHOD("is_open"), &AcousticPortal3D::is_open);
	ClassDB::bind_method(D_METHOD("set_transmission_override", "material"), &AcousticPortal3D::set_transmission_override);
	ClassDB::bind_method(D_METHOD("get_transmission_override"), &AcousticPortal3D::get_transmission_override);

	ClassDB::bind_method(D_METHOD("get_aperture_area"), &AcousticPortal3D::get_aperture_area);
	ClassDB::bind_method(D_METHOD("get_world_center"), &AcousticPortal3D::get_world_center);
	ClassDB::bind_method(D_METHOD("get_world_normal"), &AcousticPortal3D::get_world_normal);
	ClassDB::bind_method(D_METHOD("closest_point_on_aperture", "world_point"), &AcousticPortal3D::closest_point_on_aperture);

	ClassDB::bind_static_method("AcousticPortal3D", D_METHOD("get_portal_count"), &AcousticPortal3D::get_portal_count);
	ClassDB::bind_static_method("AcousticPortal3D", D_METHOD("get_portal", "index"), &AcousticPortal3D::get_portal);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "aperture_size", PROPERTY_HINT_NONE, "suffix:m"), "set_aperture_size", "get_aperture_size");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "room_a_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AcousticRoom3D"), "set_room_a_path", "get_room_a_path");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "room_b_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AcousticRoom3D"), "set_room_b_path", "get_room_b_path");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "open"), "set_open", "is_open");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "transmission_override", PROPERTY_HINT_RESOURCE_TYPE, "AcousticMaterial"), "set_transmission_override", "get_transmission_override");
}
