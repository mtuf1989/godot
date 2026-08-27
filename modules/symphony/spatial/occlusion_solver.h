#ifndef OCCLUSION_SOLVER_H
#define OCCLUSION_SOLVER_H

#include "core/math/vector3.h"
#include "core/templates/vector.h"
#include "servers/physics_3d/direct_states/physics_direct_space_state_3d.h"

class AcousticMaterial;

// Computes direct-path occlusion and 3-band material transmission between
// a source and listener using alternating-direction ray marching.
//
// Corrects the reference addon's double-counting bug by:
// 1. Alternating ray direction (even hits: source→listener, odd: listener→source)
// 2. Taking sqrt of accumulated transmission when hit_count > 1
//    (compensates for hitting both entry and exit faces of a wall)
//
// Uses PhysicsServer3D direct space state queries — no RayCast3D nodes.
class OcclusionSolver {
public:
	struct Config {
		int max_hits = 8;                // Maximum ray steps before giving up
		uint32_t collision_mask = 1;     // Physics collision mask for occlusion rays
		float ray_offset = 0.02f;        // Offset past hit point to avoid re-hitting same face
		float fallback_transmission_low = 0.1f;   // Used when collider has no AcousticBody3D
		float fallback_transmission_mid = 0.05f;
		float fallback_transmission_high = 0.03f;
	};

	struct Result {
		float transmission[3] = { 1.0f, 1.0f, 1.0f }; // Low/Mid/High [0,1]
		float occlusion = 0.0f;         // 0=clear, 1=fully occluded (derived from transmission)
		int hit_count = 0;              // Number of surfaces intersected
		bool total_absorption_hit = false; // A soundproof wall was encountered
		float total_absorption_speed = 2.5f; // Transition speed from the blocking material
	};

	// Solve occlusion for a single source→listener path.
	// p_space: the physics direct space state (from World3D)
	// p_source: emitter world position
	// p_listener: listener world position
	// p_exclude: RIDs to exclude (e.g., listener's CharacterBody3D)
	// p_config: solver configuration
	static Result solve(
			PhysicsDirectSpaceState3D *p_space,
			const Vector3 &p_source,
			const Vector3 &p_listener,
			const Vector<RID> &p_exclude,
			const Config &p_config);
};

#endif // OCCLUSION_SOLVER_H
