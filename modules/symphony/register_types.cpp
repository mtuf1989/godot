#include "register_types.h"

#include "stream/audio_stream_symphony.h"
#include "stream/audio_stream_playback_symphony.h"

#include "core/object/class_db.h"

void initialize_symphony_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(AudioStreamSymphony);
		GDREGISTER_CLASS(AudioStreamPlaybackSymphony);
	}
}

void uninitialize_symphony_module(ModuleInitializationLevel p_level) {
}
