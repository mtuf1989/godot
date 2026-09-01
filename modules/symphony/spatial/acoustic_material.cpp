#include "acoustic_material.h"
#include "core/object/class_db.h"

void AcousticMaterial::_bind_methods() {
	// Absorption
	ClassDB::bind_method(D_METHOD("set_absorption_low", "value"), &AcousticMaterial::set_absorption_low);
	ClassDB::bind_method(D_METHOD("get_absorption_low"), &AcousticMaterial::get_absorption_low);
	ClassDB::bind_method(D_METHOD("set_absorption_mid", "value"), &AcousticMaterial::set_absorption_mid);
	ClassDB::bind_method(D_METHOD("get_absorption_mid"), &AcousticMaterial::get_absorption_mid);
	ClassDB::bind_method(D_METHOD("set_absorption_high", "value"), &AcousticMaterial::set_absorption_high);
	ClassDB::bind_method(D_METHOD("get_absorption_high"), &AcousticMaterial::get_absorption_high);

	// Scattering
	ClassDB::bind_method(D_METHOD("set_scattering", "value"), &AcousticMaterial::set_scattering);
	ClassDB::bind_method(D_METHOD("get_scattering"), &AcousticMaterial::get_scattering);

	// Transmission
	ClassDB::bind_method(D_METHOD("set_transmission_low", "value"), &AcousticMaterial::set_transmission_low);
	ClassDB::bind_method(D_METHOD("get_transmission_low"), &AcousticMaterial::get_transmission_low);
	ClassDB::bind_method(D_METHOD("set_transmission_mid", "value"), &AcousticMaterial::set_transmission_mid);
	ClassDB::bind_method(D_METHOD("get_transmission_mid"), &AcousticMaterial::get_transmission_mid);
	ClassDB::bind_method(D_METHOD("set_transmission_high", "value"), &AcousticMaterial::set_transmission_high);
	ClassDB::bind_method(D_METHOD("get_transmission_high"), &AcousticMaterial::get_transmission_high);

	// Total absorption
	ClassDB::bind_method(D_METHOD("set_total_absorption", "value"), &AcousticMaterial::set_total_absorption);
	ClassDB::bind_method(D_METHOD("get_total_absorption"), &AcousticMaterial::get_total_absorption);
	ClassDB::bind_method(D_METHOD("set_total_absorption_transition_speed", "value"), &AcousticMaterial::set_total_absorption_transition_speed);
	ClassDB::bind_method(D_METHOD("get_total_absorption_transition_speed"), &AcousticMaterial::get_total_absorption_transition_speed);

	// Utility
	ClassDB::bind_method(D_METHOD("get_mean_absorption"), &AcousticMaterial::get_mean_absorption);

	// Static preset constructor
	ClassDB::bind_static_method("AcousticMaterial", D_METHOD("create_preset", "preset"), &AcousticMaterial::create_preset);

	// Properties — Absorption group
	ADD_GROUP("Absorption", "absorption_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "absorption_low", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_absorption_low", "get_absorption_low");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "absorption_mid", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_absorption_mid", "get_absorption_mid");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "absorption_high", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_absorption_high", "get_absorption_high");

	// Properties — Scattering group
	ADD_GROUP("Scattering", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scattering", PROPERTY_HINT_RANGE, "0.0,1.0,0.01"), "set_scattering", "get_scattering");

	// Properties — Transmission group
	ADD_GROUP("Transmission", "transmission_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transmission_low", PROPERTY_HINT_RANGE, "0.0,1.0,0.001"), "set_transmission_low", "get_transmission_low");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transmission_mid", PROPERTY_HINT_RANGE, "0.0,1.0,0.001"), "set_transmission_mid", "get_transmission_mid");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transmission_high", PROPERTY_HINT_RANGE, "0.0,1.0,0.001"), "set_transmission_high", "get_transmission_high");

	// Properties — Special group
	ADD_GROUP("Special", "total_absorption");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "total_absorption"), "set_total_absorption", "get_total_absorption");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "total_absorption_transition_speed", PROPERTY_HINT_RANGE, "0.1,20.0,0.1"), "set_total_absorption_transition_speed", "get_total_absorption_transition_speed");

	// Enum constants
	BIND_ENUM_CONSTANT(PRESET_GENERIC);
	BIND_ENUM_CONSTANT(PRESET_CONCRETE);
	BIND_ENUM_CONSTANT(PRESET_WOOD);
	BIND_ENUM_CONSTANT(PRESET_GLASS);
	BIND_ENUM_CONSTANT(PRESET_CARPET);
	BIND_ENUM_CONSTANT(PRESET_METAL);
	BIND_ENUM_CONSTANT(PRESET_BRICK);
	BIND_ENUM_CONSTANT(PRESET_PLASTER);
	BIND_ENUM_CONSTANT(PRESET_ACOUSTIC_FOAM);
	BIND_ENUM_CONSTANT(PRESET_CURTAIN);
	BIND_ENUM_CONSTANT(PRESET_MARBLE);
	BIND_ENUM_CONSTANT(PRESET_TILE);
}

Ref<AcousticMaterial> AcousticMaterial::create_preset(Preset p_preset) {
	Ref<AcousticMaterial> m;
	m.instantiate();

	switch (p_preset) {
		case PRESET_GENERIC:
			// Default values are already generic.
			break;

		case PRESET_CONCRETE:
			m->absorption_low = 0.05f;
			m->absorption_mid = 0.07f;
			m->absorption_high = 0.08f;
			m->scattering = 0.05f;
			m->transmission_low = 0.010f;
			m->transmission_mid = 0.006f;
			m->transmission_high = 0.004f;
			break;

		case PRESET_WOOD:
			m->absorption_low = 0.11f;
			m->absorption_mid = 0.07f;
			m->absorption_high = 0.06f;
			m->scattering = 0.05f;
			m->transmission_low = 0.050f;
			m->transmission_mid = 0.010f;
			m->transmission_high = 0.004f;
			break;

		case PRESET_GLASS:
			m->absorption_low = 0.25f;
			m->absorption_mid = 0.06f;
			m->absorption_high = 0.03f;
			m->scattering = 0.05f;
			m->transmission_low = 0.045f;
			m->transmission_mid = 0.030f;
			m->transmission_high = 0.008f;
			break;

		case PRESET_CARPET:
			m->absorption_low = 0.24f;
			m->absorption_mid = 0.69f;
			m->absorption_high = 0.73f;
			m->scattering = 0.57f;
			m->transmission_low = 0.015f;
			m->transmission_mid = 0.004f;
			m->transmission_high = 0.002f;
			break;

		case PRESET_METAL:
			m->absorption_low = 0.20f;
			m->absorption_mid = 0.07f;
			m->absorption_high = 0.06f;
			m->scattering = 0.05f;
			m->transmission_low = 0.140f;
			m->transmission_mid = 0.018f;
			m->transmission_high = 0.007f;
			break;

		case PRESET_BRICK:
			m->absorption_low = 0.03f;
			m->absorption_mid = 0.04f;
			m->absorption_high = 0.07f;
			m->scattering = 0.05f;
			m->transmission_low = 0.016f;
			m->transmission_mid = 0.010f;
			m->transmission_high = 0.006f;
			break;

		case PRESET_PLASTER:
			m->absorption_low = 0.12f;
			m->absorption_mid = 0.06f;
			m->absorption_high = 0.04f;
			m->scattering = 0.05f;
			m->transmission_low = 0.040f;
			m->transmission_mid = 0.020f;
			m->transmission_high = 0.004f;
			break;

		case PRESET_ACOUSTIC_FOAM:
			m->absorption_low = 1.00f;
			m->absorption_mid = 1.00f;
			m->absorption_high = 1.00f;
			m->scattering = 0.60f;
			m->transmission_low = 0.000f;
			m->transmission_mid = 0.000f;
			m->transmission_high = 0.000f;
			m->total_absorption = true;
			m->total_absorption_transition_speed = 1.2f;
			break;

		case PRESET_CURTAIN:
			// Heavy fabric curtain: high absorption (especially mids/highs),
			// moderate scattering, some low-frequency transmission.
			// Reference: ISO 354 measurements for heavy drapes.
			m->absorption_low = 0.14f;
			m->absorption_mid = 0.35f;
			m->absorption_high = 0.55f;
			m->scattering = 0.45f;
			m->transmission_low = 0.100f;
			m->transmission_mid = 0.050f;
			m->transmission_high = 0.020f;
			break;

		case PRESET_MARBLE:
			// Polished marble: very low absorption (highly reflective),
			// low scattering, very low transmission (dense stone).
			// Reference: Acoustic properties of polished stone surfaces.
			m->absorption_low = 0.01f;
			m->absorption_mid = 0.01f;
			m->absorption_high = 0.02f;
			m->scattering = 0.05f;
			m->transmission_low = 0.007f;
			m->transmission_mid = 0.004f;
			m->transmission_high = 0.003f;
			break;

		case PRESET_TILE:
			// Ceramic/porcelain tile: very low absorption, low scattering,
			// moderate transmission (thinner than concrete).
			// Reference: Similar to ceramic in the addon.
			m->absorption_low = 0.01f;
			m->absorption_mid = 0.02f;
			m->absorption_high = 0.02f;
			m->scattering = 0.05f;
			m->transmission_low = 0.045f;
			m->transmission_mid = 0.030f;
			m->transmission_high = 0.008f;
			break;

		default:
			break;
	}

	return m;
}
