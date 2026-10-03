#include "tests/test_macros.h"
TEST_FORCE_LINK(test_agent_capabilities)
#include "modules/modules_enabled.gen.h"
#ifdef MODULE_AGENT_TOOLS_ENABLED
#include "modules/agent_tools/agent_tools.h"
#include "core/object/class_db.h"
TEST_CASE("[AgentTools] Capabilities reflect actual native classes") {
    const Dictionary result = AgentTools::capabilities();
    CHECK(int(result["protocol_version"]) == 1);
    const Dictionary classes = result["classes"];
    CHECK(bool(classes["AudioStreamSymphony"]) == ClassDB::class_exists("AudioStreamSymphony"));
    CHECK(bool(classes["GDScript"]) == ClassDB::class_exists("GDScript"));
    const Dictionary features = result["features"];
    CHECK(bool(features["capabilities"]));
}
#endif
