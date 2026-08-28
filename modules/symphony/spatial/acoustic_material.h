#ifndef ACOUSTIC_MATERIAL_H
#define ACOUSTIC_MATERIAL_H

#include "core/io/resource.h"

// Defines how a surface interacts with sound — absorption, scattering, and transmission.
// Three frequency bands aligned to Steam Audio: Low (≤400 Hz), Mid (400–2500 Hz), High (≥2500 Hz).
class AcousticMaterial : public Resource {
	GDCLASS(AcousticMaterial, Resource);

public:
	// Preset identifiers for static constructors.
	enum Preset {
		PRESET_GENERIC = 0,
		PRESET_CONCRETE,
		PRESET_WOOD,
		PRESET_GLASS,
		PRESET_CARPET,
		PRESET_METAL,
		PRESET_BRICK,
		PRESET_PLASTER,
		PRESET_ACOUSTIC_FOAM,
		PRESET_CURTAIN,
		PRESET_MARBLE,
		PRESET_TILE,
		PRESET_MAX,
	};

private:
	// Absorption: fraction of energy absorbed on reflection per band [0,1].
	float absorption_low = 0.10f;
	float absorption_mid = 0.20f;
	float absorption_high = 0.30f;

	// Scattering: how diffusely the surface reflects. 0=specular, 1=fully diffuse.
	// RESERVED (Phase 6, Task 6): authored and inspector-visible, but deliberately
	// unused by the current engine — no diffuse-reflection path exists yet. It is
	// kept in the material model so future scattered/late-reflection work (e.g. a
	// diffuse-rain or ray-traced reflection stage) can consume it without a
	// resource-format migration. Do NOT wire it into occlusion/RT60; those use
	// absorption + transmission only.
	float scattering = 0.05f;

	// Transmission: fraction of energy passing through per band [0,1].
	// 0=fully blocks, 1=fully transparent.
	float transmission_low = 0.100f;
	float transmission_mid = 0.050f;
	float transmission_high = 0.030f;

	// Total absorption: treats surface as soundproof for occlusion.
	bool total_absorption = false;
	float total_absorption_transition_speed = 2.5f;

protected:
	static void _bind_methods();

public:
	// --- Absorption ---
	void set_absorption_low(float p_value) { absorption_low = CLAMP(p_value, 0.0f, 1.0f); }
	float get_absorption_low() const { return absorption_low; }

	void set_absorption_mid(float p_value) { absorption_mid = CLAMP(p_value, 0.0f, 1.0f); }
	float get_absorption_mid() const { return absorption_mid; }

	void set_absorption_high(float p_value) { absorption_high = CLAMP(p_value, 0.0f, 1.0f); }
	float get_absorption_high() const { return absorption_high; }

	// --- Scattering ---
	void set_scattering(float p_value) { scattering = CLAMP(p_value, 0.0f, 1.0f); }
	float get_scattering() const { return scattering; }

	// --- Transmission ---
	void set_transmission_low(float p_value) { transmission_low = CLAMP(p_value, 0.0f, 1.0f); }
	float get_transmission_low() const { return transmission_low; }

	void set_transmission_mid(float p_value) { transmission_mid = CLAMP(p_value, 0.0f, 1.0f); }
	float get_transmission_mid() const { return transmission_mid; }

	void set_transmission_high(float p_value) { transmission_high = CLAMP(p_value, 0.0f, 1.0f); }
	float get_transmission_high() const { return transmission_high; }

	// --- Total Absorption ---
	void set_total_absorption(bool p_value) { total_absorption = p_value; }
	bool get_total_absorption() const { return total_absorption; }

	void set_total_absorption_transition_speed(float p_value) { total_absorption_transition_speed = CLAMP(p_value, 0.1f, 20.0f); }
	float get_total_absorption_transition_speed() const { return total_absorption_transition_speed; }

	// --- Utility ---
	// Mean absorption across bands (for Sabine RT60 when per-band isn't needed).
	float get_mean_absorption() const { return (absorption_low + absorption_mid + absorption_high) / 3.0f; }

	// --- Preset constructors ---
	static Ref<AcousticMaterial> create_preset(Preset p_preset);
};

VARIANT_ENUM_CAST(AcousticMaterial::Preset);

#endif // ACOUSTIC_MATERIAL_H
