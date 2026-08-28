#ifndef PORTAL_ROUTER_H
#define PORTAL_ROUTER_H

#include "core/math/vector3.h"
#include "core/templates/local_vector.h"

// Portal routing + diffraction math (Task 15, Phase S6).
//
// Pure functions operating on already-resolved geometry (portal centres,
// normals, apertures along a solved path). Decoupled from the SceneTree and the
// acoustics engine so the perceptual model is unit-testable in isolation. The
// engine (SpatialAcousticsEngine) resolves the path via the portal graph, then
// feeds the per-hop geometry here to produce apparent_position, an attenuation
// scalar, and a diffraction LPF cutoff.

// One resolved hop along the path: the portal's world centre + normal + aperture area.
struct PortalHop {
	Vector3 center;
	Vector3 normal; // world-space aperture normal (unit)
	float aperture_area = 1.0f; // m²
	// Point on the finite aperture rectangle nearest the listener (Phase 6, Task 5).
	// For apparent-position on WIDE apertures the panner should point at the
	// nearest edge of a big arch, not its centre. Defaults to `center` when the
	// engine does not resolve a distinct closest point.
	Vector3 apparent = Vector3();
	bool has_apparent = false;
};

class PortalRouter {
public:
	// --- Apparent position ---
	// The listener should hear an out-of-room source as if it emanates from the
	// LAST portal on the path (the opening nearest the listener). Returns the
	// true source position when there are no hops (same room).
	static Vector3 apparent_position(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops);

	// --- Per-hop aperture + incidence attenuation ---
	// Each hop attenuates energy by (a) how small the aperture is relative to a
	// reference area, and (b) how obliquely the path crosses the aperture (the
	// incidence angle between the traversal direction and the portal normal).
	// Returns a linear gain in [0,1]. No hops → 1.0 (unattenuated, same room).
	//   p_reference_area: aperture area (m²) that passes energy unattenuated.
	static float path_gain(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops, float p_reference_area = 2.0f);

	// --- Diffraction LPF (Steam Audio DeviationModel) ---
	// Total angular deviation along the path (how much the sound "bends" around
	// corners) maps to a low-pass cutoff. A straight path keeps full bandwidth;
	// large cumulative bends roll off highs. Returns a cutoff in Hz.
	//   p_max_cutoff: open-path cutoff (e.g. 20 kHz).
	//   p_min_cutoff: cutoff at maximum deviation (e.g. 700 Hz).
	static float diffraction_cutoff(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops, float p_max_cutoff = 20000.0f, float p_min_cutoff = 700.0f);

	// Total absolute turn angle (radians) accumulated along
	// source → hop0 → hop1 → ... → listener. 0 for a straight path / same room.
	static float total_deviation(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops);
};

#endif // PORTAL_ROUTER_H
