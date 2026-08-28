#ifndef ACOUSTIC_ROOM_3D_H
#define ACOUSTIC_ROOM_3D_H

#include "scene/3d/node_3d.h"
#include "acoustic_material.h"
#include "core/templates/local_vector.h"
#include "core/templates/hash_map.h"
#include "core/object/object_id.h"

// AcousticRoom3D (Task 13, Phase S6) — Node3D describing an enclosed acoustic
// space for portal propagation and shoebox early reflections.
//
// NOTE: Was Area3D through S6; changed to Node3D (Correctness Plan Phase 1.1)
// because membership is a pure OBB test (point_in_box) that never used Area3D
// physics — the Area3D base only added a headless-test SIGSEGV. The
// set_room_priority/get_room_priority names are kept (rather than reverting to
// set_priority/get_priority) purely to avoid churn; the Area3D::set_priority
// collision that originally forced them is gone.
//
// Authoring model:
//   • bounds     — axis-aligned half-extents (local space), the room's volume.
//   • material   — the dominant surface material set for RT60 / reflections.
//   • shoebox    — optional authored room dimensions (W,H,D). If unset (zero),
//                  the engine falls back to the Task 9 ray-fan estimate.
//   • reverb_preset_override — optional forced material for reverb tuning.
//   • priority   — for overlapping rooms, the highest-priority room wins the
//                  point-in-room membership query.
//
// A static registry (room ObjectID → AcousticRoom3D*) supports O(1) membership
// resolution from the portal graph and the acoustics engine without walking the
// scene tree. Written on the main thread (enter/exit tree, transform changes).
class AcousticRoom3D : public Node3D {
	GDCLASS(AcousticRoom3D, Node3D);

private:
	Vector3 bounds = Vector3(5.0f, 3.0f, 5.0f); // half-extents (m), local space
	Ref<AcousticMaterial> material;
	Vector3 shoebox_dimensions; // authored W,H,D (m); zero = unauthored (estimate)
	Ref<AcousticMaterial> reverb_preset_override;
	int priority = 0;

	// Static registry of all rooms currently in the tree.
	static LocalVector<AcousticRoom3D *> rooms;

	// Membership cache: listener/emitter positions rarely change room frame to
	// frame. We cache (position hash cell → room id) with an epoch that bumps on
	// any room add/remove/move so a stale cache never resolves to a freed room.
	static uint64_t registry_epoch;

	void _register();
	void _unregister();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// --- Authoring properties ---
	void set_bounds(const Vector3 &p_bounds);
	Vector3 get_bounds() const { return bounds; }

	void set_material(const Ref<AcousticMaterial> &p_material) { material = p_material; }
	Ref<AcousticMaterial> get_material() const { return material; }

	void set_shoebox_dimensions(const Vector3 &p_dims);
	Vector3 get_shoebox_dimensions() const { return shoebox_dimensions; }
	bool has_authored_shoebox() const { return shoebox_dimensions.x > 0.0f && shoebox_dimensions.y > 0.0f && shoebox_dimensions.z > 0.0f; }

	void set_reverb_preset_override(const Ref<AcousticMaterial> &p_material) { reverb_preset_override = p_material; }
	Ref<AcousticMaterial> get_reverb_preset_override() const { return reverb_preset_override; }

	void set_room_priority(int p_priority);
	int get_room_priority() const { return priority; }

	// --- Membership ---
	// True if p_world_point is inside this room's oriented bounding box.
	bool contains_point(const Vector3 &p_world_point) const;

	// Pure geometry: point-in-oriented-box, decoupled from tree membership so it
	// is unit-testable without a live SceneTree/physics world.
	static bool point_in_box(const Transform3D &p_room_xform, const Vector3 &p_half_extents, const Vector3 &p_world_point);

	// Resolve which room a world point belongs to, honoring priority for overlaps.
	// Returns nullptr if the point is in no room. O(#rooms) — small in practice.
	static AcousticRoom3D *find_room_for_point(const Vector3 &p_world_point);

	// Registry access for the portal graph / engine.
	static int get_room_count() { return (int)rooms.size(); }
	static AcousticRoom3D *get_room(int p_index);
	static uint64_t get_registry_epoch() { return registry_epoch; }

	AcousticRoom3D();
	~AcousticRoom3D();
};

#endif // ACOUSTIC_ROOM_3D_H
