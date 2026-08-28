#include "portal_router.h"

#include "core/math/math_funcs.h"

Vector3 PortalRouter::apparent_position(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops) {
	if (p_hops.is_empty()) {
		return p_true_source; // same room — hear the true position
	}
	// The last hop is the portal nearest the listener; the sound arrives from it.
	return p_hops[p_hops.size() - 1].center;
}

float PortalRouter::path_gain(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops, float p_reference_area) {
	if (p_hops.is_empty()) {
		return 1.0f;
	}
	const float ref = MAX(p_reference_area, 0.0001f);

	// Build the polyline source → hop centres → listener to derive per-hop
	// traversal directions for the incidence term.
	float gain = 1.0f;
	for (uint32_t i = 0; i < p_hops.size(); i++) {
		const PortalHop &hop = p_hops[i];

		// (a) Aperture-area term: smaller openings pass less energy, capped at 1.
		const float area_term = CLAMP(hop.aperture_area / ref, 0.0f, 1.0f);

		// (b) Incidence term: cos of the angle between the traversal direction
		// through the portal and the portal normal. Grazing crossings pass less.
		Vector3 incoming = (i == 0) ? (hop.center - p_true_source) : (hop.center - p_hops[i - 1].center);
		Vector3 outgoing = (i + 1 < p_hops.size()) ? (p_hops[i + 1].center - hop.center) : (p_listener - hop.center);
		Vector3 through = (incoming.normalized() + outgoing.normalized());
		if (through.length() < 1e-4f) {
			through = outgoing;
		}
		through.normalize();
		float incidence = Math::abs(through.dot(hop.normal.normalized()));
		// Keep a floor so a perfectly grazing hit doesn't fully mute.
		incidence = CLAMP(incidence, 0.15f, 1.0f);

		gain *= area_term * incidence;
	}
	return CLAMP(gain, 0.0f, 1.0f);
}

float PortalRouter::total_deviation(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops) {
	if (p_hops.is_empty()) {
		return 0.0f;
	}
	// Build the full polyline and sum the absolute turn angle at each interior
	// vertex (each portal centre).
	LocalVector<Vector3> pts;
	pts.push_back(p_true_source);
	for (uint32_t i = 0; i < p_hops.size(); i++) {
		pts.push_back(p_hops[i].center);
	}
	pts.push_back(p_listener);

	float deviation = 0.0f;
	for (uint32_t i = 1; i + 1 < pts.size(); i++) {
		Vector3 a = (pts[i] - pts[i - 1]);
		Vector3 b = (pts[i + 1] - pts[i]);
		if (a.length() < 1e-5f || b.length() < 1e-5f) {
			continue;
		}
		float cos_a = CLAMP(a.normalized().dot(b.normalized()), -1.0f, 1.0f);
		deviation += Math::acos(cos_a); // turn angle at this vertex
	}
	return deviation;
}

float PortalRouter::diffraction_cutoff(const Vector3 &p_true_source, const Vector3 &p_listener, const LocalVector<PortalHop> &p_hops, float p_max_cutoff, float p_min_cutoff) {
	if (p_hops.is_empty()) {
		return p_max_cutoff; // straight, same room — full bandwidth
	}
	const float deviation = total_deviation(p_true_source, p_listener, p_hops);
	// Map cumulative deviation [0, π] → [max, min] cutoff. Beyond π (a full
	// reversal) we clamp at the minimum. Linear in angle is a good first model
	// (Steam Audio's DeviationModel; the perceptual curve can be tuned later).
	const float t = CLAMP(deviation / Math::PI, 0.0f, 1.0f);
	return Math::lerp(p_max_cutoff, p_min_cutoff, t);
}
