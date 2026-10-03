#include "agent_tools.h"
#include "build_identity.gen.h"
#include "core/config/engine.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/version.h"

Dictionary AgentTools::capabilities() {
	Dictionary result;
	result["schema_version"] = 1;
	result["protocol_version"] = 1;
	result["engine_version"] = GODOT_VERSION_FULL_BUILD;
	result["build"] = JSON::parse_string(AGENT_BUILD_IDENTITY);
	Dictionary classes;
	LocalVector<StringName> registered;
	ClassDB::get_class_list(registered);
	for (const StringName &name : registered) {
		classes[name] = true;
	}
	for (const char *name : {"AudioStreamSymphony", "SoundEvent", "MusicStateGraph", "Node", "SceneTree", "GDScript"}) {
		classes[name] = ClassDB::class_exists(name);
	}
	result["classes"] = classes;
	Dictionary features;
	features["capabilities"] = true;
	features["report"] = true;
	features["fail_on_script_error"] = true;
	features["strict_resources"] = true;
	result["features"] = features;
	return result;
}
