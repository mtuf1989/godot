#include "sound_event.h"
#include "core/object/class_db.h"

void SoundEvent::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_streams", "streams"), &SoundEvent::set_streams);
	ClassDB::bind_method(D_METHOD("get_streams"), &SoundEvent::get_streams);
	ClassDB::bind_method(D_METHOD("set_variation_mode", "mode"), &SoundEvent::set_variation_mode);
	ClassDB::bind_method(D_METHOD("get_variation_mode"), &SoundEvent::get_variation_mode);
	ClassDB::bind_method(D_METHOD("set_pitch_range", "range"), &SoundEvent::set_pitch_range);
	ClassDB::bind_method(D_METHOD("get_pitch_range"), &SoundEvent::get_pitch_range);
	ClassDB::bind_method(D_METHOD("set_volume_range", "range"), &SoundEvent::set_volume_range);
	ClassDB::bind_method(D_METHOD("get_volume_range"), &SoundEvent::get_volume_range);
	ClassDB::bind_method(D_METHOD("set_priority", "priority"), &SoundEvent::set_priority);
	ClassDB::bind_method(D_METHOD("get_priority"), &SoundEvent::get_priority);
	ClassDB::bind_method(D_METHOD("set_max_voices", "max_voices"), &SoundEvent::set_max_voices);
	ClassDB::bind_method(D_METHOD("get_max_voices"), &SoundEvent::get_max_voices);
	ClassDB::bind_method(D_METHOD("set_steal_mode", "mode"), &SoundEvent::set_steal_mode);
	ClassDB::bind_method(D_METHOD("get_steal_mode"), &SoundEvent::get_steal_mode);
	ClassDB::bind_method(D_METHOD("set_cooldown_ms", "ms"), &SoundEvent::set_cooldown_ms);
	ClassDB::bind_method(D_METHOD("get_cooldown_ms"), &SoundEvent::get_cooldown_ms);
	ClassDB::bind_method(D_METHOD("set_category", "category"), &SoundEvent::set_category);
	ClassDB::bind_method(D_METHOD("get_category"), &SoundEvent::get_category);
	ClassDB::bind_method(D_METHOD("set_bus_override", "bus"), &SoundEvent::set_bus_override);
	ClassDB::bind_method(D_METHOD("get_bus_override"), &SoundEvent::get_bus_override);
	ClassDB::bind_method(D_METHOD("set_spatial_mode", "mode"), &SoundEvent::set_spatial_mode);
	ClassDB::bind_method(D_METHOD("get_spatial_mode"), &SoundEvent::get_spatial_mode);
	ClassDB::bind_method(D_METHOD("set_max_distance", "distance"), &SoundEvent::set_max_distance);
	ClassDB::bind_method(D_METHOD("get_max_distance"), &SoundEvent::get_max_distance);
	ClassDB::bind_method(D_METHOD("set_loop", "loop"), &SoundEvent::set_loop);
	ClassDB::bind_method(D_METHOD("get_loop"), &SoundEvent::get_loop);
	ClassDB::bind_method(D_METHOD("set_virtualize_when_inaudible", "virtualize"), &SoundEvent::set_virtualize_when_inaudible);
	ClassDB::bind_method(D_METHOD("get_virtualize_when_inaudible"), &SoundEvent::get_virtualize_when_inaudible);

	ClassDB::bind_method(D_METHOD("set_rtpc_bindings", "bindings"), &SoundEvent::set_rtpc_bindings);
	ClassDB::bind_method(D_METHOD("get_rtpc_bindings"), &SoundEvent::get_rtpc_bindings);
	ClassDB::bind_method(D_METHOD("get_rtpc_binding_count"), &SoundEvent::get_rtpc_binding_count);

	ADD_GROUP("Streams", "");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "streams", PROPERTY_HINT_TYPE_STRING, String::num(Variant::OBJECT) + "/" + String::num(PROPERTY_HINT_RESOURCE_TYPE) + ":AudioStream"), "set_streams", "get_streams");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "variation_mode", PROPERTY_HINT_ENUM, "Random,Sequence,Shuffle"), "set_variation_mode", "get_variation_mode");

	ADD_GROUP("Playback", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "pitch_range"), "set_pitch_range", "get_pitch_range");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "volume_range"), "set_volume_range", "get_volume_range");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "loop"), "set_loop", "get_loop");

	ADD_GROUP("Voice Management", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "priority", PROPERTY_HINT_RANGE, "0,100,1"), "set_priority", "get_priority");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_voices", PROPERTY_HINT_RANGE, "0,64,1"), "set_max_voices", "get_max_voices");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "steal_mode", PROPERTY_HINT_ENUM, "Oldest,Quietest,Farthest"), "set_steal_mode", "get_steal_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cooldown_ms", PROPERTY_HINT_RANGE, "0,5000,1"), "set_cooldown_ms", "get_cooldown_ms");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "virtualize_when_inaudible"), "set_virtualize_when_inaudible", "get_virtualize_when_inaudible");

	ADD_GROUP("Routing", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "category", PROPERTY_HINT_ENUM, "SFX,Music,UI,Ambient,Voice"), "set_category", "get_category");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bus_override"), "set_bus_override", "get_bus_override");

	ADD_GROUP("Spatial", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spatial_mode", PROPERTY_HINT_ENUM, "Non-Positional,2D,3D"), "set_spatial_mode", "get_spatial_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_distance", PROPERTY_HINT_RANGE, "0,10000,1"), "set_max_distance", "get_max_distance");

	ADD_GROUP("RTPC", "");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "rtpc_bindings", PROPERTY_HINT_TYPE_STRING, String::num(Variant::DICTIONARY) + ":"), "set_rtpc_bindings", "get_rtpc_bindings");

	BIND_ENUM_CONSTANT(VARIATION_RANDOM);
	BIND_ENUM_CONSTANT(VARIATION_SEQUENCE);
	BIND_ENUM_CONSTANT(VARIATION_SHUFFLE);
	BIND_ENUM_CONSTANT(STEAL_OLDEST);
	BIND_ENUM_CONSTANT(STEAL_QUIETEST);
	BIND_ENUM_CONSTANT(STEAL_FARTHEST);
	BIND_ENUM_CONSTANT(CATEGORY_SFX);
	BIND_ENUM_CONSTANT(CATEGORY_MUSIC);
	BIND_ENUM_CONSTANT(CATEGORY_UI);
	BIND_ENUM_CONSTANT(CATEGORY_AMBIENT);
	BIND_ENUM_CONSTANT(CATEGORY_VOICE);
	BIND_ENUM_CONSTANT(SPATIAL_NON_POSITIONAL);
	BIND_ENUM_CONSTANT(SPATIAL_2D);
	BIND_ENUM_CONSTANT(SPATIAL_3D);

	BIND_ENUM_CONSTANT(RTPC_PITCH);
	BIND_ENUM_CONSTANT(RTPC_VOLUME);
	BIND_ENUM_CONSTANT(RTPC_FILTER_CUTOFF);
	BIND_ENUM_CONSTANT(RTPC_GRAPH_INPUT);
	BIND_ENUM_CONSTANT(RTPC_PLAYBACK_SPEED);
}
