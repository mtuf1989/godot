#ifndef ROOM_ESTIMATOR_H
#define ROOM_ESTIMATOR_H

#include "core/math/math_funcs.h"
#include "core/math/vector3.h"
#include "core/templates/vector.h"
#include "servers/physics_3d/direct_states/physics_direct_space_state_3d.h"

class AcousticMaterial;

// Material absorption accessor used by the header-only compute<>() template.
// Defined in room_estimator.cpp where AcousticMaterial is fully visible.
// Writes mid- and high-band absorption; total-absorption materials clamp to 1.
void room_estimator_material_absorption(const AcousticMaterial *p_mat, float p_fallback, float &r_mid, float &r_high);

// Estimates room acoustics (volume, surface absorption, RT60) by casting a
// Fibonacci-sphere ray fan from a probe point and analyzing the hits.
//
// Physical model:
// - N rays uniformly cover the sphere; each subtends solid angle 4π/N.
// - Volume via sum-of-cones:   V = Σ (1/3) · dᵢ³ · (4π/N)
// - Surface via sum-of-patches: S = Σ dᵢ² · (4π/N)
// - Absorbed area:              Sα = Σ αᵢ · dᵢ² · (4π/N)
// - Sabine RT60:                RT60 = 0.161 · V / (Sα)
// - Eyring correction (high α): RT60 = 0.161 · V / (−S · ln(1 − ᾱ))
//
// Escaped rays (no hit within max_distance) contribute to openness and are
// treated as fully absorptive (open sky = infinite absorption).
class RoomEstimator {
public:
	struct Config {
		int ray_count = 32;             // Number of Fibonacci rays
		float max_distance = 100.0f;    // Ray length; beyond = escaped
		uint32_t collision_mask = 1;    // Physics mask
		bool ignore_floor = false;      // Skip downward rays. Default off (3.4): the
		                                // solid-angle renormalization makes the integral
		                                // self-consistent, so a complete sphere is preferred;
		                                // the knob stays available for floor-dominated probes.
		float floor_angle_threshold = 45.0f; // Degrees below horizon to ignore
		float eyring_threshold = 0.3f;  // Mean absorption above which Eyring is used
		float fallback_absorption = 0.15f; // Absorption for hits with no AcousticMaterial
	};

	struct Result {
		float rt60 = 0.0f;            // Reverberation time (seconds)
		float volume = 0.0f;          // Estimated room volume (m³)
		float surface_area = 0.0f;    // Estimated total surface area (m²)
		float mean_absorption = 0.0f; // Area-weighted mean absorption coefficient
		float openness = 0.0f;        // Ratio of escaped rays [0=sealed, 1=open sky]
		float high_band_absorption = 0.0f; // Mean high-band absorption (for reverb damping)
		int rays_cast = 0;
		int rays_hit = 0;
	};

	// Generate `count` unit directions on a sphere via golden-angle spiral.
	// Ported from the reference addon (verified correct).
	static void generate_fibonacci_sphere(int p_count, Vector<Vector3> &r_directions);

	// Probe the room from a point against the physics world. Thin wrapper around
	// compute<RaycastFn>() that supplies a PhysicsServer3D raycast functor.
	static Result estimate(
			PhysicsDirectSpaceState3D *p_space,
			const Vector3 &p_probe_position,
			const Vector<RID> &p_exclude,
			const Config &p_config);

	// Core room-estimation computation, decoupled from the physics server via an
	// injectable raycast functor:
	//   bool p_raycast(const Vector3 &from, const Vector3 &to,
	//                  Vector3 &r_pos, AcousticMaterial **r_mat)
	//     → true on hit; writes hit position to r_pos and the collider's
	//       AcousticMaterial* (nullptr for untagged) to r_mat.
	template <typename RaycastFn>
	static Result compute(
			const Vector3 &p_probe_position,
			const Config &p_config,
			RaycastFn &&p_raycast) {
		Result result;

		if (p_config.ray_count <= 0) {
			return result;
		}

		Vector<Vector3> directions;
		generate_fibonacci_sphere(p_config.ray_count, directions);

		float floor_cos_threshold = Math::cos(Math::deg_to_rad(p_config.floor_angle_threshold));

		// Fixed-size stack scratch (ray_count is clamped to <= 128 by the engine
		// config setter) — no heap allocation in the solve path (Task 5 promise).
		static constexpr int MAX_RAYS = 128;
		int ray_n = MIN(directions.size(), MAX_RAYS);
		float hit_distance[MAX_RAYS];
		float hit_abs_mid[MAX_RAYS];
		float hit_abs_high[MAX_RAYS];
		bool ray_escaped[MAX_RAYS];
		bool ray_active[MAX_RAYS];

		// --- Pass 1: cast all rays, record hits, accumulate mean hit distance. ---
		int active_rays = 0;
		int hit_rays = 0;
		int escaped_rays = 0;
		float sum_hit_distance = 0.0f;

		for (int i = 0; i < ray_n; i++) {
			const Vector3 &dir = directions[i];
			if (p_config.ignore_floor && dir.y <= -floor_cos_threshold) {
				ray_active[i] = false;
				continue;
			}
			ray_active[i] = true;
			active_rays++;

			Vector3 to = p_probe_position + dir * p_config.max_distance;
			Vector3 hit_pos;
			AcousticMaterial *mat = nullptr;
			bool hit = p_raycast(p_probe_position, to, hit_pos, &mat);

			if (!hit) {
				ray_escaped[i] = true;
				escaped_rays++;
			} else {
				ray_escaped[i] = false;
				hit_rays++;
				float d = p_probe_position.distance_to(hit_pos);
				hit_distance[i] = d;
				sum_hit_distance += d;
				room_estimator_material_absorption(mat, p_config.fallback_absorption, hit_abs_mid[i], hit_abs_high[i]);
			}
		}

		result.rays_cast = active_rays;
		result.rays_hit = hit_rays;

		if (active_rays == 0) {
			return result;
		}

		// All rays escaped → the probe is in open space. Openness 1, no reverb.
		if (hit_rays == 0) {
			result.openness = 1.0f;
			result.rt60 = 0.0f;
			return result;
		}

		// Phase 3.3: escaped rays represent an opening at the room boundary — a
		// perfect absorber *at the wall*, not 100 m away. Assign them the mean
		// hit distance with absorption 1.0 so an opening SHORTENS RT60 (energy
		// leaves through the aperture) instead of inflating the volume integral.
		const float mean_hit_distance = sum_hit_distance / (float)hit_rays;

		// Phase 3.4: renormalize the solid angle to the actual covered fraction
		// of the sphere (active, non-floor-filtered rays), so the integral is
		// self-consistent whether or not ignore_floor is set.
		const float solid_angle_per_ray = (4.0f * (float)Math::PI) / (float)active_rays;

		// --- Pass 2: integrate volume / surface / absorption. ---
		float total_volume = 0.0f;
		float total_surface = 0.0f;
		float total_absorbed = 0.0f;
		float total_high_absorbed = 0.0f;

		for (int i = 0; i < ray_n; i++) {
			if (!ray_active[i]) {
				continue;
			}
			float distance;
			float absorption_mid;
			float absorption_high;
			if (ray_escaped[i]) {
				distance = mean_hit_distance;
				absorption_mid = 1.0f;
				absorption_high = 1.0f;
			} else {
				distance = hit_distance[i];
				absorption_mid = hit_abs_mid[i];
				absorption_high = hit_abs_high[i];
			}

			float cone_volume = (1.0f / 3.0f) * distance * distance * distance * solid_angle_per_ray;
			float patch_surface = distance * distance * solid_angle_per_ray;

			total_volume += cone_volume;
			total_surface += patch_surface;
			total_absorbed += absorption_mid * patch_surface;
			total_high_absorbed += absorption_high * patch_surface;
		}

		if (total_surface < 0.001f) {
			return result;
		}

		result.openness = (float)escaped_rays / (float)active_rays;
		result.volume = total_volume;
		result.surface_area = total_surface;
		result.mean_absorption = CLAMP(total_absorbed / total_surface, 0.0f, 1.0f);
		result.high_band_absorption = CLAMP(total_high_absorbed / total_surface, 0.0f, 1.0f);

		const float SABINE_CONST = 0.161f;

		if (result.mean_absorption >= p_config.eyring_threshold) {
			float one_minus_alpha = 1.0f - result.mean_absorption;
			if (one_minus_alpha < 0.001f) {
				one_minus_alpha = 0.001f;
			}
			float denom = -total_surface * Math::log(one_minus_alpha);
			if (denom > 0.001f) {
				result.rt60 = SABINE_CONST * total_volume / denom;
			}
		} else {
			if (total_absorbed > 0.001f) {
				result.rt60 = SABINE_CONST * total_volume / total_absorbed;
			}
		}

		result.rt60 = CLAMP(result.rt60, 0.0f, 10.0f);

		if (result.openness > 0.8f) {
			result.rt60 *= (1.0f - result.openness) / 0.2f;
			result.rt60 = MAX(result.rt60, 0.0f);
		}

		return result;
	}

	// Compute reverb send level from openness.
	// Enclosed (openness=0) → high send; open (openness=1) → low send.
	static float openness_to_reverb_send(float p_openness);
};

#endif // ROOM_ESTIMATOR_H
