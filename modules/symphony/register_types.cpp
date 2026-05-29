#include "register_types.h"

#include "stream/audio_stream_symphony.h"
#include "stream/audio_stream_playback_symphony.h"
#include "core/symphony_operator_registry.h"

#include "nodes/generators/symphony_oscillator.h"
#include "nodes/generators/symphony_constant.h"
#include "nodes/generators/symphony_noise.h"
#include "nodes/generators/symphony_lfo.h"
#include "nodes/filters/symphony_biquad_filter.h"
#include "nodes/filters/symphony_one_pole.h"
#include "nodes/filters/symphony_dc_blocker.h"
#include "nodes/filters/symphony_saturator.h"
#include "nodes/envelopes/symphony_gain.h"
#include "nodes/envelopes/symphony_adsr.h"
#include "nodes/envelopes/symphony_compressor.h"
#include "nodes/math/symphony_math_add.h"
#include "nodes/math/symphony_mix.h"
#include "nodes/math/symphony_map_range.h"
#include "nodes/math/symphony_sample_hold.h"
#include "nodes/timing/symphony_clock.h"
#include "nodes/timing/symphony_trigger_delay.h"
#include "nodes/io/symphony_graph_input.h"
#include "nodes/io/symphony_graph_output.h"
#include "nodes/io/symphony_trigger_input.h"

#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/symphony_editor_plugin.h"
#endif

void initialize_symphony_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// Create operator registry and register all built-in operators
		OperatorRegistry::create_singleton();

		// Generators
		SymphonyOscillator::register_operator();
		SymphonyConstant::register_operator();
		SymphonyNoise::register_operator();
		SymphonyLFO::register_operator();

		// Filters
		SymphonyBiquadFilter::register_operator();
		SymphonyOnePole::register_operator();
		SymphonyDCBlocker::register_operator();
		SymphonySaturator::register_operator();

		// Envelopes & Dynamics
		SymphonyGain::register_operator();
		SymphonyADSR::register_operator();
		SymphonyCompressor::register_operator();

		// Math
		SymphonyMathAdd::register_operator();
		SymphonyMix::register_operator();
		SymphonyMapRange::register_operator();
		SymphonySampleHold::register_operator();

		// Timing
		SymphonyClock::register_operator();
		SymphonyTriggerDelay::register_operator();

		// I/O
		SymphonyGraphOutput::register_operator();
		SymphonyGraphInput::register_operator();
		SymphonyTriggerInput::register_operator();

		// Register Godot classes
		GDREGISTER_CLASS(AudioStreamSymphony);
		GDREGISTER_CLASS(AudioStreamPlaybackSymphony);
#ifdef TOOLS_ENABLED
	} else if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_CLASS(SymphonyNodeInspectorProxy);
		GDREGISTER_CLASS(SymphonyGraphEditor);
		EditorPlugins::add_by_type<SymphonyEditorPlugin>();
#endif
	}
}

void uninitialize_symphony_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		OperatorRegistry::destroy_singleton();
	}
}
