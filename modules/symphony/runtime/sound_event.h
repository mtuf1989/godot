#ifndef SOUND_EVENT_H
#define SOUND_EVENT_H

#include "core/io/resource.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/resources/curve.h"

class SoundEvent : public Resource {
	GDCLASS(SoundEvent, Resource);

public:
	enum VariationMode { VARIATION_RANDOM = 0, VARIATION_SEQUENCE, VARIATION_SHUFFLE };
	enum StealMode { STEAL_OLDEST = 0, STEAL_QUIETEST, STEAL_FARTHEST };
	enum Category { CATEGORY_SFX = 0, CATEGORY_MUSIC, CATEGORY_UI, CATEGORY_AMBIENT, CATEGORY_VOICE };
	enum SpatialMode { SPATIAL_NON_POSITIONAL = 0, SPATIAL_2D, SPATIAL_3D };
	enum AttenuationModel { ATTENUATION_LINEAR = 0, ATTENUATION_LOGARITHMIC, ATTENUATION_CUSTOM };
	enum RTPCTarget { RTPC_PITCH = 0, RTPC_VOLUME, RTPC_GRAPH_INPUT, RTPC_PLAYBACK_SPEED };

private:
	TypedArray<AudioStream> streams;
	VariationMode variation_mode = VARIATION_RANDOM;
	Vector2 pitch_range = Vector2(1.0, 1.0);
	Vector2 volume_range = Vector2(0.0, 0.0);
	int priority = 50;
	int max_voices = 0;
	StealMode steal_mode = STEAL_OLDEST;
	float cooldown_ms = 0.0;
	Category category = CATEGORY_SFX;
	StringName bus_override;
	float importance_weight = 1.0f; // Base importance multiplier for voice stealing/virtualization
	SpatialMode spatial_mode = SPATIAL_NON_POSITIONAL;
	AttenuationModel attenuation_model = ATTENUATION_LINEAR;
	Ref<Curve> attenuation_curve; // Used only when attenuation_model == ATTENUATION_CUSTOM
	float max_distance = 2000.0;
	bool loop = false;
	bool virtualize_when_inaudible = true;

	// RTPC bindings — serialized as Array of Dictionaries for .tres compatibility.
	// Each dict: {parameter_name, target, curve, min_value, max_value, graph_input_name}
	TypedArray<Dictionary> rtpc_bindings;

protected:
	static void _bind_methods();

public:
	void set_streams(const TypedArray<AudioStream> &p_streams) { streams = p_streams; }
	TypedArray<AudioStream> get_streams() const { return streams; }

	void set_variation_mode(VariationMode p_mode) { variation_mode = p_mode; }
	VariationMode get_variation_mode() const { return variation_mode; }

	void set_pitch_range(const Vector2 &p_range) { pitch_range = p_range; }
	Vector2 get_pitch_range() const { return pitch_range; }

	void set_volume_range(const Vector2 &p_range) { volume_range = p_range; }
	Vector2 get_volume_range() const { return volume_range; }

	void set_priority(int p_priority) { priority = p_priority; }
	int get_priority() const { return priority; }

	void set_max_voices(int p_max) { max_voices = p_max; }
	int get_max_voices() const { return max_voices; }

	void set_steal_mode(StealMode p_mode) { steal_mode = p_mode; }
	StealMode get_steal_mode() const { return steal_mode; }

	void set_cooldown_ms(float p_ms) { cooldown_ms = p_ms; }
	float get_cooldown_ms() const { return cooldown_ms; }

	void set_category(Category p_category) { category = p_category; }
	Category get_category() const { return category; }

	void set_bus_override(const StringName &p_bus) { bus_override = p_bus; }
	StringName get_bus_override() const { return bus_override; }

	void set_importance_weight(float p_weight) { importance_weight = p_weight; }
	float get_importance_weight() const { return importance_weight; }

	void set_spatial_mode(SpatialMode p_mode) { spatial_mode = p_mode; }
	SpatialMode get_spatial_mode() const { return spatial_mode; }

	void set_attenuation_model(AttenuationModel p_model) { attenuation_model = p_model; }
	AttenuationModel get_attenuation_model() const { return attenuation_model; }

	void set_attenuation_curve(const Ref<Curve> &p_curve) { attenuation_curve = p_curve; }
	Ref<Curve> get_attenuation_curve() const { return attenuation_curve; }

	void set_max_distance(float p_dist) { max_distance = p_dist; }
	float get_max_distance() const { return max_distance; }

	void set_loop(bool p_loop) { loop = p_loop; }
	bool get_loop() const { return loop; }

	void set_virtualize_when_inaudible(bool p_virt) { virtualize_when_inaudible = p_virt; }
	bool get_virtualize_when_inaudible() const { return virtualize_when_inaudible; }

	// RTPC bindings
	void set_rtpc_bindings(const TypedArray<Dictionary> &p_bindings) { rtpc_bindings = p_bindings; }
	TypedArray<Dictionary> get_rtpc_bindings() const { return rtpc_bindings; }

	// Convenience: get binding count
	int get_rtpc_binding_count() const { return rtpc_bindings.size(); }

	// --- Offset/Additive Volume Model ---
	// Computes the final volume in dB using the standard middleware (Wwise/FMOD) additive approach:
	//   Final volume = random_offset_db + rtpc_volume_db + bus_volume_db
	// All offsets stack additively in the dB domain.
	//
	// - p_random_offset_db: dB offset from volume_range randomization (per-instance).
	// - p_rtpc_volume_db:   dB offset from RTPC_VOLUME binding curve evaluation.
	//
	// Bus volume is applied separately by BusController; this helper covers the event-local offsets.
	static float compute_final_volume_db(float p_random_offset_db, float p_rtpc_volume_db);
};

VARIANT_ENUM_CAST(SoundEvent::VariationMode);
VARIANT_ENUM_CAST(SoundEvent::StealMode);
VARIANT_ENUM_CAST(SoundEvent::Category);
VARIANT_ENUM_CAST(SoundEvent::SpatialMode);
VARIANT_ENUM_CAST(SoundEvent::AttenuationModel);
VARIANT_ENUM_CAST(SoundEvent::RTPCTarget);

#endif // SOUND_EVENT_H
