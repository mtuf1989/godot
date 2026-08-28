#ifndef OCCLUSION_SOLVER_H
#define OCCLUSION_SOLVER_H

#include "core/math/math_funcs.h"
#include "core/math/vector3.h"
#include "core/templates/vector.h"
#include "servers/physics_3d/direct_states/physics_direct_space_state_3d.h"

class AcousticMaterial;

// Material accessors used by the header-only compute<>() template. Defined in
// occlusion_solver.cpp where AcousticMaterial's full definition is visible, so
// the template does not need to include acoustic_material.h.
//   is_total_absorption: returns true if the material is a total absorber and
//     writes its transition speed to r_speed.
//   transmission: writes the material's low/mid/high transmission.
bool occlusion_material_is_total_absorption(const AcousticMaterial *p_mat, float &r_speed);
void occlusion_material_transmission(const AcousticMaterial *p_mat, float &r_low, float &r_mid, float &r_high);

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

	// Solve occlusion for a single source→listener path against the physics world.
	// Thin wrapper around compute<RaycastFn>() that supplies a PhysicsServer3D
	// raycast functor (+ AcousticBody3D material lookup).
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

	// Core direct-path occlusion computation, decoupled from the physics server
	// via an injectable raycast functor:
	//   bool p_raycast(const Vector3 &from, const Vector3 &to,
	//                  Vector3 &r_pos, AcousticMaterial **r_mat)
	//     → returns true on a hit; on a hit writes the hit position to r_pos and
	//       the hit collider's AcousticMaterial* (or nullptr for untagged) to
	//       r_mat. The physics-backed solve() wraps this with an intersect_ray +
	//       AcousticBody3D::lookup_material; tests hand back synthetic materials.
	template <typename RaycastFn>
	static Result compute(
			const Vector3 &p_source,
			const Vector3 &p_listener,
			const Config &p_config,
			RaycastFn &&p_raycast) {
		Result result;

		float total_distance = p_source.distance_to(p_listener);
		if (total_distance < 0.001f) {
			return result; // Source and listener at same point.
		}

		// Accumulated transmission per band (multiplicative).
		float accum_low = 1.0f;
		float accum_mid = 1.0f;
		float accum_high = 1.0f;

		// Ray march state — alternating direction (Steam Audio pattern).
		// Even hits: source → listener direction; odd hits: listener → source.
		Vector3 forward_pos = p_source;
		Vector3 backward_pos = p_listener;

		int hit_count = 0;

		for (int step = 0; step < p_config.max_hits; step++) {
			bool forward = (step % 2 == 0);

			Vector3 from, to;
			if (forward) {
				from = forward_pos;
				to = p_listener;
			} else {
				from = backward_pos;
				to = p_source;
			}

			// Skip if from and to are too close (converged).
			if (from.distance_to(to) < p_config.ray_offset * 2.0f) {
				break;
			}

			Vector3 hit_pos;
			AcousticMaterial *mat = nullptr;
			bool hit = p_raycast(from, to, hit_pos, &mat);
			if (!hit) {
				break; // Clear path in this direction — done.
			}

			// Verify the hit is between the endpoints (not behind us).
			float hit_dist_from_source = p_source.distance_to(hit_pos);
			if (hit_dist_from_source >= total_distance) {
				break; // Hit is beyond the listener.
			}

			hit_count++;

			float t_low, t_mid, t_high;
			bool is_total_absorption = false;
			float ta_speed = 2.5f;

			if (mat != nullptr) {
				if (occlusion_material_is_total_absorption(mat, ta_speed)) {
					is_total_absorption = true;
					t_low = 0.0f;
					t_mid = 0.0f;
					t_high = 0.0f;
				} else {
					occlusion_material_transmission(mat, t_low, t_mid, t_high);
				}
			} else {
				t_low = p_config.fallback_transmission_low;
				t_mid = p_config.fallback_transmission_mid;
				t_high = p_config.fallback_transmission_high;
			}

			accum_low *= t_low;
			accum_mid *= t_mid;
			accum_high *= t_high;

			if (is_total_absorption) {
				result.total_absorption_hit = true;
				result.total_absorption_speed = ta_speed;
				break;
			}

			// Advance the march position past the hit.
			Vector3 advance_dir = (to - from).normalized();
			Vector3 new_pos = hit_pos + advance_dir * p_config.ray_offset;
			if (forward) {
				forward_pos = new_pos;
			} else {
				backward_pos = new_pos;
			}
		}

		result.hit_count = hit_count;

		// sqrt correction for double-counting wall entry/exit faces.
		if (hit_count > 1 && !result.total_absorption_hit) {
			accum_low = Math::sqrt(accum_low);
			accum_mid = Math::sqrt(accum_mid);
			accum_high = Math::sqrt(accum_high);
		}

		result.transmission[0] = CLAMP(accum_low, 0.0f, 1.0f);
		result.transmission[1] = CLAMP(accum_mid, 0.0f, 1.0f);
		result.transmission[2] = CLAMP(accum_high, 0.0f, 1.0f);

		float mean_transmission = (result.transmission[0] + result.transmission[1] + result.transmission[2]) / 3.0f;
		result.occlusion = 1.0f - mean_transmission;

		return result;
	}

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
