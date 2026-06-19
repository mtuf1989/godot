#include "register_types.h"

#include "cip_action.h"
#include "cip_action_mapping.h"
#include "cip_mapping_context.h"
#include "cip_modifier.h"
#include "cip_modifier_dead_zone.h"
#include "cip_modifier_negate.h"
#include "cip_modifier_scale.h"
#include "cip_modifier_swizzle.h"
#include "cip_subsystem.h"
#include "cip_trigger.h"
#include "cip_trigger_hold.h"
#include "cip_trigger_pressed.h"
#include "cip_trigger_released.h"

#include "core/object/class_db.h"

void initialize_cip_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(CIPAction);
	GDREGISTER_CLASS(CIPModifier);
	GDREGISTER_CLASS(CIPModifierDeadZone);
	GDREGISTER_CLASS(CIPModifierNegate);
	GDREGISTER_CLASS(CIPModifierScale);
	GDREGISTER_CLASS(CIPModifierSwizzle);
	GDREGISTER_CLASS(CIPTrigger);
	GDREGISTER_CLASS(CIPTriggerPressed);
	GDREGISTER_CLASS(CIPTriggerReleased);
	GDREGISTER_CLASS(CIPTriggerHold);
	GDREGISTER_CLASS(CIPActionMapping);
	GDREGISTER_CLASS(CIPMappingContext);
	GDREGISTER_CLASS(CIPSubsystem);
}

void uninitialize_cip_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
}
