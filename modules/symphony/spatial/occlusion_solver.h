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

	// --- Volumetric occlusion (Task 12) ---
	// Graduated occlusion for a finite-size source, following Steam Audio's
	// volumetricOcclusion: sample points within the source's radius *volume*,
	// validate each is visible from the source centre first (reject samples
	// buried inside geometry), then test listener visibility. The occlusion
	// scalar is 1 - (visible_fraction). A point source (radius ≈ 0) or a solve
	// with no valid samples falls back to a single centre→listener ray so the
	// result degrades gracefully to binary occlusion.
	struct VolumetricConfig {
		int sample_count = 16;        // Number of volume samples (scaled by budget)
		uint32_t collision_mask = 1;  // Physics collision mask
		float min_radius = 0.05f;     // Below this the source is treated as a point
	};

	struct VolumetricResult {
		float occlusion = 0.0f;    // 0 = fully audible, 1 = fully blocked
		int samples_taken = 0;     // Volume samples that were visible from the centre
		int samples_visible = 0;   // Of those, how many reached the listener
		int rays_issued = 0;       // Total rays cast (for scheduler accounting)
	};

	// Generate `count` deterministic sample offsets that fill a unit sphere
	// volume (Fibonacci spiral shell with cube-root radial spacing so the
	// samples are evenly distributed by volume, not clustered at the centre).
	// Offsets are in unit-sphere space; multiply by the source radius.
	static void generate_volume_samples(int p_count, Vector<Vector3> &r_offsets);

	static VolumetricResult solve_volumetric(
			PhysicsDirectSpaceState3D *p_space,
			const Vector3 &p_source,
			float p_source_radius,
			const Vector3 &p_listener,
			const Vector<RID> &p_exclude,
			const VolumetricConfig &p_config);

	// Core volumetric-occlusion computation, decoupled from the physics server
	// via an injectable line-of-sight predicate `p_clear_los(a, b) -> bool`
	// (true = unobstructed). The physics-backed solve_volumetric() above is a
	// thin wrapper around this; tests drive it with a synthetic predicate to
	// verify the graduated-occlusion math without a live PhysicsServer3D.
	template <typename ClearLosFn>
	static VolumetricResult compute_volumetric(
			const Vector3 &p_source,
			float p_source_radius,
			const Vector3 &p_listener,
			int p_sample_count,
			float p_min_radius,
			ClearLosFn &&p_clear_los) {
		VolumetricResult result;

		// Point source (or below the minimum radius): single centre→listener test.
		if (p_source_radius < p_min_radius || p_sample_count <= 1) {
			result.rays_issued = 1;
			result.samples_taken = 1;
			bool visible = p_clear_los(p_source, p_listener);
			result.samples_visible = visible ? 1 : 0;
			result.occlusion = visible ? 0.0f : 1.0f;
			return result;
		}

		Vector<Vector3> offsets;
		generate_volume_samples(p_sample_count, offsets);

		int valid = 0;   // samples visible from the source centre
		int visible = 0; // of those, samples with clear LOS to the listener
		int rays = 0;

		for (int i = 0; i < offsets.size(); i++) {
			Vector3 sample = p_source + offsets[i] * p_source_radius;

			// 1. Validate the sample is visible from the source centre. A sample
			//    buried inside geometry is rejected so occlusion reflects only
			//    the audible portion of the source.
			rays++;
			if (!p_clear_los(p_source, sample)) {
				continue;
			}
			valid++;

			// 2. Test listener visibility from this valid sample point.
			rays++;
			if (p_clear_los(sample, p_listener)) {
				visible++;
			}
		}

		result.rays_issued = rays;
		result.samples_taken = valid;
		result.samples_visible = visible;

		if (valid == 0) {
			// Entire sampled volume is inside geometry — fall back to a centre test.
			result.rays_issued += 1;
			bool centre_visible = p_clear_los(p_source, p_listener);
			result.samples_taken = 1;
			result.samples_visible = centre_visible ? 1 : 0;
			result.occlusion = centre_visible ? 0.0f : 1.0f;
			return result;
		}

		float visible_fraction = (float)visible / (float)valid;
		result.occlusion = CLAMP(1.0f - visible_fraction, 0.0f, 1.0f);
		return result;
	}
};

#endif // OCCLUSION_SOLVER_H
