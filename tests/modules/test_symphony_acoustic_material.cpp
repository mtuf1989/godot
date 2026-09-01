/**************************************************************************/
/*  test_symphony_acoustic_material.cpp                                   */
/*  Suite: [Symphony][Spatial][Material] — presets + property round-trip. */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_acoustic_material)

#include "modules/symphony/spatial/acoustic_material.h"
#include "core/io/resource.h"

namespace TestSymphonyAcousticMaterial {

// Copy every STORAGE property from src → dst via the property list — this is
// exactly the data path a .tres save/load exercises, without touching disk.
static void round_trip(const Ref<AcousticMaterial> &p_src, Ref<AcousticMaterial> &p_dst) {
	List<PropertyInfo> props;
	p_src->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (!(pi.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}
		bool valid = false;
		Variant v = p_src->get(pi.name, &valid);
		if (valid) {
			p_dst->set(pi.name, v);
		}
	}
}

// --- Preset sanity ------------------------------------------------------

TEST_CASE("[Symphony][Spatial][Material] Default equals the generic preset") {
	Ref<AcousticMaterial> def;
	def.instantiate();
	Ref<AcousticMaterial> generic = AcousticMaterial::create_preset(AcousticMaterial::PRESET_GENERIC);
	CHECK(def->get_absorption_low() == doctest::Approx(generic->get_absorption_low()));
	CHECK(def->get_absorption_mid() == doctest::Approx(generic->get_absorption_mid()));
	CHECK(def->get_absorption_high() == doctest::Approx(generic->get_absorption_high()));
	CHECK(def->get_transmission_low() == doctest::Approx(generic->get_transmission_low()));
	CHECK(def->get_transmission_mid() == doctest::Approx(generic->get_transmission_mid()));
	CHECK(def->get_transmission_high() == doctest::Approx(generic->get_transmission_high()));
	CHECK(def->get_total_absorption() == generic->get_total_absorption());
}

TEST_CASE("[Symphony][Spatial][Material] All 12 presets are valid and in range") {
	for (int p = 0; p < AcousticMaterial::PRESET_MAX; p++) {
		Ref<AcousticMaterial> m = AcousticMaterial::create_preset((AcousticMaterial::Preset)p);
		REQUIRE(m.is_valid());
		// All coefficients within [0,1].
		CHECK(m->get_absorption_low() >= 0.0f);
		CHECK(m->get_absorption_low() <= 1.0f);
		CHECK(m->get_absorption_mid() >= 0.0f);
		CHECK(m->get_absorption_mid() <= 1.0f);
		CHECK(m->get_absorption_high() >= 0.0f);
		CHECK(m->get_absorption_high() <= 1.0f);
		CHECK(m->get_transmission_low() >= 0.0f);
		CHECK(m->get_transmission_low() <= 1.0f);
		CHECK(m->get_transmission_mid() >= 0.0f);
		CHECK(m->get_transmission_mid() <= 1.0f);
		CHECK(m->get_transmission_high() >= 0.0f);
		CHECK(m->get_transmission_high() <= 1.0f);
		CHECK(m->get_scattering() >= 0.0f);
		CHECK(m->get_scattering() <= 1.0f);
		CHECK(m->get_mean_absorption() >= 0.0f);
		CHECK(m->get_mean_absorption() <= 1.0f);
	}
}

TEST_CASE("[Symphony][Spatial][Material] Foam is a total absorber; concrete is not") {
	Ref<AcousticMaterial> foam = AcousticMaterial::create_preset(AcousticMaterial::PRESET_ACOUSTIC_FOAM);
	Ref<AcousticMaterial> concrete = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CONCRETE);
	CHECK(foam->get_total_absorption());
	CHECK_FALSE(concrete->get_total_absorption());
	// Concrete blocks far more than a curtain (transmission much lower).
	Ref<AcousticMaterial> curtain = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CURTAIN);
	CHECK(concrete->get_transmission_mid() < curtain->get_transmission_mid());
}

TEST_CASE("[Symphony][Spatial][Material] Absorptive materials differ from reflective ones") {
	Ref<AcousticMaterial> carpet = AcousticMaterial::create_preset(AcousticMaterial::PRESET_CARPET);
	Ref<AcousticMaterial> marble = AcousticMaterial::create_preset(AcousticMaterial::PRESET_MARBLE);
	// Carpet absorbs highs strongly; marble is highly reflective.
	CHECK(carpet->get_absorption_high() > marble->get_absorption_high());
	CHECK(carpet->get_mean_absorption() > marble->get_mean_absorption());
}

// --- Clamping -----------------------------------------------------------

TEST_CASE("[Symphony][Spatial][Material] Setters clamp coefficients to [0,1]") {
	Ref<AcousticMaterial> m;
	m.instantiate();
	m->set_absorption_low(5.0f);
	m->set_absorption_mid(-1.0f);
	m->set_transmission_high(2.0f);
	m->set_scattering(-0.5f);
	CHECK(m->get_absorption_low() == doctest::Approx(1.0f));
	CHECK(m->get_absorption_mid() == doctest::Approx(0.0f));
	CHECK(m->get_transmission_high() == doctest::Approx(1.0f));
	CHECK(m->get_scattering() == doctest::Approx(0.0f));
}

TEST_CASE("[Symphony][Spatial][Material] Transition speed clamps to its authored range") {
	Ref<AcousticMaterial> m;
	m.instantiate();
	m->set_total_absorption_transition_speed(100.0f);
	CHECK(m->get_total_absorption_transition_speed() <= 20.0f);
	m->set_total_absorption_transition_speed(0.0f);
	CHECK(m->get_total_absorption_transition_speed() >= 0.1f);
}

// --- Property round-trip (the .tres data path) -------------------------

TEST_CASE("[Symphony][Spatial][Material] All 12 presets round-trip through the property list") {
	for (int p = 0; p < AcousticMaterial::PRESET_MAX; p++) {
		Ref<AcousticMaterial> src = AcousticMaterial::create_preset((AcousticMaterial::Preset)p);
		Ref<AcousticMaterial> dst;
		dst.instantiate();
		round_trip(src, dst);

		CHECK(dst->get_absorption_low() == doctest::Approx(src->get_absorption_low()));
		CHECK(dst->get_absorption_mid() == doctest::Approx(src->get_absorption_mid()));
		CHECK(dst->get_absorption_high() == doctest::Approx(src->get_absorption_high()));
		CHECK(dst->get_scattering() == doctest::Approx(src->get_scattering()));
		CHECK(dst->get_transmission_low() == doctest::Approx(src->get_transmission_low()));
		CHECK(dst->get_transmission_mid() == doctest::Approx(src->get_transmission_mid()));
		CHECK(dst->get_transmission_high() == doctest::Approx(src->get_transmission_high()));
		CHECK(dst->get_total_absorption() == src->get_total_absorption());
		CHECK(dst->get_total_absorption_transition_speed() == doctest::Approx(src->get_total_absorption_transition_speed()));
	}
}

} // namespace TestSymphonyAcousticMaterial
