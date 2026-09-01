#ifndef ACOUSTIC_GIZMOS_H
#define ACOUSTIC_GIZMOS_H

#ifdef TOOLS_ENABLED

#include "editor/plugins/editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_gizmos.h"

// Task 13b (Phase S6) — editor gizmos for the acoustic authoring nodes.
//
// AcousticRoom3DGizmoPlugin draws the room's oriented bounding box (from its
// half-extent `bounds`). AcousticPortal3DGizmoPlugin draws the aperture
// rectangle on the portal's local XY plane plus a short normal indicator.
//
// These are the correct replacement for the reference addon's
// ImmediateMesh-in-_physics_process hack: gizmos are editor-only, drawn only
// when selected, and add zero runtime cost.

class AcousticRoom3DGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(AcousticRoom3DGizmoPlugin, EditorNode3DGizmoPlugin);

public:
	bool has_gizmo(Node3D *p_spatial) override;
	String get_gizmo_name() const override;
	int get_priority() const override;
	void redraw(EditorNode3DGizmo *p_gizmo) override;

	AcousticRoom3DGizmoPlugin();
};

class AcousticPortal3DGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(AcousticPortal3DGizmoPlugin, EditorNode3DGizmoPlugin);

public:
	bool has_gizmo(Node3D *p_spatial) override;
	String get_gizmo_name() const override;
	int get_priority() const override;
	void redraw(EditorNode3DGizmo *p_gizmo) override;

	AcousticPortal3DGizmoPlugin();
};

// Editor plugin that registers both gizmo plugins with Node3DEditor.
class AcousticGizmosEditorPlugin : public EditorPlugin {
	GDCLASS(AcousticGizmosEditorPlugin, EditorPlugin);

public:
	AcousticGizmosEditorPlugin();
};

#endif // TOOLS_ENABLED

#endif // ACOUSTIC_GIZMOS_H
