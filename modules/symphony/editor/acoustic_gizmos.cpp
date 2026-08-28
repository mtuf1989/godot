#include "acoustic_gizmos.h"

#ifdef TOOLS_ENABLED

#include "../spatial/acoustic_portal_3d.h"
#include "../spatial/acoustic_room_3d.h"

#include "editor/scene/3d/node_3d_editor_plugin.h"

// =============================================================================
// AcousticRoom3DGizmoPlugin — draws the room's oriented bounding box.
// =============================================================================

AcousticRoom3DGizmoPlugin::AcousticRoom3DGizmoPlugin() {
	// Soft cyan for room bounds.
	create_material("acoustic_room_material", Color(0.2f, 0.8f, 0.9f, 0.8f));
}

bool AcousticRoom3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<AcousticRoom3D>(p_spatial) != nullptr;
}

String AcousticRoom3DGizmoPlugin::get_gizmo_name() const {
	return "AcousticRoom3D";
}

int AcousticRoom3DGizmoPlugin::get_priority() const {
	return -1;
}

void AcousticRoom3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();

	AcousticRoom3D *room = Object::cast_to<AcousticRoom3D>(p_gizmo->get_node_3d());
	if (room == nullptr) {
		return;
	}

	const Vector3 half = room->get_bounds();
	AABB aabb;
	aabb.position = -half;
	aabb.size = half * 2.0f;

	Vector<Vector3> lines;
	for (int i = 0; i < 12; i++) {
		Vector3 a, b;
		aabb.get_edge(i, a, b);
		lines.push_back(a);
		lines.push_back(b);
	}

	p_gizmo->add_lines(lines, get_material("acoustic_room_material", p_gizmo));
}

// =============================================================================
// AcousticPortal3DGizmoPlugin — draws the aperture rectangle + normal.
// =============================================================================

AcousticPortal3DGizmoPlugin::AcousticPortal3DGizmoPlugin() {
	// Warm amber for portal apertures.
	create_material("acoustic_portal_material", Color(1.0f, 0.7f, 0.2f, 0.9f));
	create_material("acoustic_portal_normal_material", Color(1.0f, 0.9f, 0.5f, 0.6f));
}

bool AcousticPortal3DGizmoPlugin::has_gizmo(Node3D *p_spatial) {
	return Object::cast_to<AcousticPortal3D>(p_spatial) != nullptr;
}

String AcousticPortal3DGizmoPlugin::get_gizmo_name() const {
	return "AcousticPortal3D";
}

int AcousticPortal3DGizmoPlugin::get_priority() const {
	return -1;
}

void AcousticPortal3DGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();

	AcousticPortal3D *portal = Object::cast_to<AcousticPortal3D>(p_gizmo->get_node_3d());
	if (portal == nullptr) {
		return;
	}

	const Vector2 ap = portal->get_aperture_size();
	const float hw = ap.x * 0.5f;
	const float hh = ap.y * 0.5f;

	// Aperture rectangle on the local XY plane (z=0).
	const Vector3 c0(-hw, -hh, 0.0f);
	const Vector3 c1(hw, -hh, 0.0f);
	const Vector3 c2(hw, hh, 0.0f);
	const Vector3 c3(-hw, hh, 0.0f);

	Vector<Vector3> lines;
	lines.push_back(c0);
	lines.push_back(c1);
	lines.push_back(c1);
	lines.push_back(c2);
	lines.push_back(c2);
	lines.push_back(c3);
	lines.push_back(c3);
	lines.push_back(c0);
	// Diagonals for visibility.
	lines.push_back(c0);
	lines.push_back(c2);
	lines.push_back(c1);
	lines.push_back(c3);
	p_gizmo->add_lines(lines, get_material("acoustic_portal_material", p_gizmo));

	// Normal indicator: a short line along local +Z from the aperture centre.
	Vector<Vector3> normal_line;
	normal_line.push_back(Vector3(0, 0, 0));
	normal_line.push_back(Vector3(0, 0, MAX(hw, hh)));
	p_gizmo->add_lines(normal_line, get_material("acoustic_portal_normal_material", p_gizmo));
}

// =============================================================================
// Editor plugin registration.
// =============================================================================

AcousticGizmosEditorPlugin::AcousticGizmosEditorPlugin() {
	Ref<AcousticRoom3DGizmoPlugin> room_gizmo = Ref<AcousticRoom3DGizmoPlugin>(memnew(AcousticRoom3DGizmoPlugin));
	Node3DEditor::get_singleton()->add_gizmo_plugin(room_gizmo);

	Ref<AcousticPortal3DGizmoPlugin> portal_gizmo = Ref<AcousticPortal3DGizmoPlugin>(memnew(AcousticPortal3DGizmoPlugin));
	Node3DEditor::get_singleton()->add_gizmo_plugin(portal_gizmo);
}

#endif // TOOLS_ENABLED
