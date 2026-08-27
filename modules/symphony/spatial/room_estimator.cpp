#include "room_estimator.h"
#include "acoustic_body_3d.h"
#include "acoustic_material.h"
#include "core/math/math_funcs.h"
#include "servers/physics_3d/physics_server_3d_types.h"

void RoomEstimator::generate_fibonacci_sphere(int p_count, Vector<Vector3> &r_directions) {
	r_directions.clear();
	if (p_count <= 0) {
		return;
	}
	r_directions.resize(p_count);

	const double golden_ratio = (1.0 + Math::sqrt(5.0)) / 2.0;

	for (int i = 0; i < p_count; i++) {
		// Polar angle — uniform in cos(θ) so points aren't bunched at poles.
		double theta = Math::acos(1.0 - 2.0 * ((double)i + 0.5) / (double)p_count);
		// Azimuthal angle — golden-angle increments for even spread.
		double phi = Math::TAU * (double)i / golden_ratio;

		r_directions.write[i] = Vector3(
				(float)(Math::sin(theta) * Math::cos(phi)),
				(float)Math::cos(theta),
				(float)(Math::sin(theta) * Math::sin(phi)));
	}
}

RoomEstimator::Result RoomEstimator::estimate(
		PhysicsDirectSpaceState3D *p_space,
		const Vector3 &p_probe_position,
		const Vector<RID> &p_exclude,
		const Config &p_config) {
	Result result;

	if (p_space == nullptr || p_config.ray_count <= 0) {
		return result;
	}

	Vector<Vector3> directions;
	generate_fibonacci_sphere(p_config.ray_count, directions);

	// Solid angle per ray (sr). Total sphere = 4π.
	const float solid_angle_per_ray = (4.0f * (float)Math::PI) / (float)p_config.ray_count;

	float floor_cos_threshold = Math::cos(Math::deg_to_rad(p_config.floor_angle_threshold));

	float total_volume = 0.0f;
	float total_surface = 0.0f;
	float total_absorbed = 0.0f;      // Σ αᵢ Sᵢ (broadband, using mean absorption)
	float total_high_absorbed = 0.0f; // Σ α_highᵢ Sᵢ (for damping)
	int escaped_rays = 0;
	int active_rays = 0;
	int hit_rays = 0;

	for (int i = 0; i < directions.size(); i++) {
		const Vector3 &dir = directions[i];

		// Skip downward rays if ignore_floor is enabled.
		if (p_config.ignore_floor && dir.y <= -floor_cos_threshold) {
			continue;
		}

		active_rays++;

		// Cast the ray.
		PS3DT::RayParameters ray_params;
		ray_params.from = p_probe_position;
		ray_params.to = p_probe_position + dir * p_config.max_distance;
		ray_params.collision_mask = p_config.collision_mask;
		ray_params.collide_with_areas = false;
		ray_params.collide_with_bodies = true;
		for (int e = 0; e < p_exclude.size(); e++) {
			ray_params.exclude.insert(p_exclude[e]);
		}

		PS3DT::RayResult ray_result;
		bool hit = p_space->intersect_ray(ray_params, ray_result);

		float distance;
		float absorption_mid;
		float absorption_high;

		if (!hit) {
			// Escaped ray — open direction. Treat as max distance, full absorption
			// (energy leaves the room and never returns).
			escaped_rays++;
			distance = p_config.max_distance;
			absorption_mid = 1.0f;
			absorption_high = 1.0f;
		} else {
			hit_rays++;
			distance = p_probe_position.distance_to(ray_result.position);

			AcousticMaterial *mat = AcousticBody3D::lookup_material(ray_result.collider_id);
			if (mat != nullptr) {
				absorption_mid = mat->get_absorption_mid();
				absorption_high = mat->get_absorption_high();
				if (mat->get_total_absorption()) {
					absorption_mid = 1.0f;
					absorption_high = 1.0f;
				}
			} else {
				absorption_mid = p_config.fallback_absorption;
				absorption_high = p_config.fallback_absorption;
			}
		}

		// Cone volume: (1/3) · d³ · solid_angle
		float cone_volume = (1.0f / 3.0f) * distance * distance * distance * solid_angle_per_ray;
		// Patch surface: d² · solid_angle
		float patch_surface = distance * distance * solid_angle_per_ray;

		total_volume += cone_volume;
		total_surface += patch_surface;
		total_absorbed += absorption_mid * patch_surface;
		total_high_absorbed += absorption_high * patch_surface;
	}

	result.rays_cast = active_rays;
	result.rays_hit = hit_rays;

	if (active_rays == 0 || total_surface < 0.001f) {
		return result;
	}

	// Openness: fraction of rays that escaped.
	result.openness = (float)escaped_rays / (float)active_rays;

	result.volume = total_volume;
	result.surface_area = total_surface;
	result.mean_absorption = CLAMP(total_absorbed / total_surface, 0.0f, 1.0f);
	result.high_band_absorption = CLAMP(total_high_absorbed / total_surface, 0.0f, 1.0f);

	// --- RT60 computation ---
	// Sabine: RT60 = 0.161 · V / (Σ Sα)
	// Eyring correction for highly absorptive rooms:
	//   RT60 = 0.161 · V / (−S · ln(1 − ᾱ))
	const float SABINE_CONST = 0.161f;

	if (result.mean_absorption >= p_config.eyring_threshold) {
		// Eyring — more accurate when absorption is high.
		float one_minus_alpha = 1.0f - result.mean_absorption;
		if (one_minus_alpha < 0.001f) {
			one_minus_alpha = 0.001f; // Avoid log(0); near-total absorption → very short RT60.
		}
		float denom = -total_surface * Math::log(one_minus_alpha);
		if (denom > 0.001f) {
			result.rt60 = SABINE_CONST * total_volume / denom;
		}
	} else {
		// Sabine — standard for moderate absorption.
		if (total_absorbed > 0.001f) {
			result.rt60 = SABINE_CONST * total_volume / total_absorbed;
		}
	}

	// Clamp RT60 to a sane range (0 to 10 seconds).
	result.rt60 = CLAMP(result.rt60, 0.0f, 10.0f);

	// Very open spaces have negligible reverb regardless of the formula.
	if (result.openness > 0.8f) {
		result.rt60 *= (1.0f - result.openness) / 0.2f; // Fade to 0 as openness → 1.0
		result.rt60 = MAX(result.rt60, 0.0f);
	}

	return result;
}

float RoomEstimator::openness_to_reverb_send(float p_openness) {
	// Enclosed (openness=0) → high send (1.0); open (openness=1) → low send (0.0).
	// Power curve so send drops off faster as openness increases (matches addon's wetness).
	float send = (1.0f - p_openness) * (1.0f - p_openness);
	return CLAMP(send, 0.0f, 1.0f);
}
