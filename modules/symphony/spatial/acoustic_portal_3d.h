#ifndef ACOUSTIC_PORTAL_3D_H
#define ACOUSTIC_PORTAL_3D_H

#include "scene/3d/node_3d.h"
#include "acoustic_material.h"
#include "core/templates/local_vector.h"

class AcousticRoom3D;

// AcousticPortal3D (Task 13, Phase S6) — an aperture (doorway, window, opening)
// connecting two AcousticRoom3Ds. Edges of the portal graph (Task 14).
//
// Authoring model:
//   • aperture_size — width (x) / height (y) of the opening, in metres. The
//     portal plane is the node's local XY plane; its normal is local +Z.
//   • room_a / room_b — NodePaths to the two AcousticRoom3Ds it connects.
//   • open — closed portals block direct propagation (transmission-only path).
//   • transmission_override — optional material governing leak through a closed
//     portal; when null, a closed portal uses a small default transmission.
//
// Geometry helpers (centre, normal, area, closest-point) feed the routing and
// per-hop attenuation in Task 15. A static registry supports graph building.
class AcousticPortal3D : public Node3D {
	GDCLASS(AcousticPortal3D, Node3D);

private:
	Vector2 aperture_size = Vector2(1.0f, 2.0f); // width, height (m)
	NodePath room_a_path;
	NodePath room_b_path;
	bool open = true;
	Ref<AcousticMaterial> transmission_override;

	static LocalVector<AcousticPortal3D *> portals;
	static uint64_t state_epoch;     // bumps on open/close or add/remove (topology → rebuild + cache flush)
	static uint64_t transform_epoch; // bumps on movement only (refresh centres in place; cache valid)

	void _register();
	void _unregister();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// --- Authoring properties ---
	void set_aperture_size(const Vector2 &p_size);
	Vector2 get_aperture_size() const { return aperture_size; }

	void set_room_a_path(const NodePath &p_path);
	NodePath get_room_a_path() const { return room_a_path; }
	void set_room_b_path(const NodePath &p_path);
	NodePath get_room_b_path() const { return room_b_path; }

	void set_open(bool p_open);
	bool is_open() const { return open; }

	void set_transmission_override(const Ref<AcousticMaterial> &p_material) { transmission_override = p_material; }
	Ref<AcousticMaterial> get_transmission_override() const { return transmission_override; }

	// --- Resolved rooms ---
	AcousticRoom3D *get_room_a() const;
	AcousticRoom3D *get_room_b() const;
	// The room on the other side of the portal from p_room, or nullptr.
	AcousticRoom3D *get_other_room(const AcousticRoom3D *p_room) const;
	// True if this portal connects the two given rooms (in either order).
	bool connects(const AcousticRoom3D *p_a, const AcousticRoom3D *p_b) const;

	// --- Geometry (world space) ---
	Vector3 get_world_center() const;
	Vector3 get_world_normal() const;         // local +Z in world space, normalized
	float get_aperture_area() const;          // width * height (m²)
	// Closest point on the (finite) aperture rectangle to an arbitrary world point.
	Vector3 closest_point_on_aperture(const Vector3 &p_world_point) const;

	// Pure geometry helpers (decoupled from tree membership for unit tests).
	static Vector3 closest_point_on_rect(const Transform3D &p_portal_xform, const Vector2 &p_aperture_size, const Vector3 &p_world_point);
	static float aperture_area(const Vector2 &p_aperture_size) { return p_aperture_size.x * p_aperture_size.y; }

	// --- Registry ---
	static int get_portal_count() { return (int)portals.size(); }
	static AcousticPortal3D *get_portal(int p_index);
	static uint64_t get_state_epoch() { return state_epoch; }
	static uint64_t get_transform_epoch() { return transform_epoch; }

	AcousticPortal3D();
	~AcousticPortal3D();
};

#endif // ACOUSTIC_PORTAL_3D_H
