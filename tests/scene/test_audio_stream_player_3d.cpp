/**************************************************************************/
/*  test_audio_stream_player_3d.cpp                                       */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_audio_stream_player_3d)

#include "core/object/class_db.h"

namespace TestAudioStreamPlayer3D {

TEST_CASE("[AudioStreamPlayer3D] ClassDB binds direct_volume_db") {
	CHECK(ClassDB::has_method("AudioStreamPlayer3D", "set_direct_volume_db"));
	CHECK(ClassDB::has_method("AudioStreamPlayer3D", "get_direct_volume_db"));
	CHECK(ClassDB::has_property("AudioStreamPlayer3D", "direct_volume_db"));
}

} // namespace TestAudioStreamPlayer3D
