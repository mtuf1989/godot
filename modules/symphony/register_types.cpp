#include "register_types.h"

#include "stream/audio_stream_symphony.h"
#include "stream/audio_stream_playback_symphony.h"
#include "core/symphony_operator_registry.h"
#include "core/symphony_voice_manager.h"
#include "core/shared_pcm_cache.h"
#include "runtime/sound_event.h"
#include "runtime/voice_manager.h"
#include "runtime/event_dispatcher.h"
#include "runtime/music_state_graph.h"
#include "runtime/beat_clock.h"
#include "runtime/rtpc_engine.h"
#include "runtime/bus_controller.h"
#include "runtime/transition_analyzer.h"

#include "nodes/generators/symphony_oscillator.h"
#include "nodes/generators/symphony_constant.h"
#include "nodes/generators/symphony_noise.h"
#include "nodes/generators/symphony_lfo.h"
#include "nodes/generators/symphony_wave_player.h"
#include "nodes/filters/symphony_biquad_filter.h"
#include "nodes/filters/symphony_one_pole.h"
#include "nodes/filters/symphony_dc_blocker.h"
#include "nodes/filters/symphony_saturator.h"
#include "nodes/filters/symphony_sv_filter.h"
#include "nodes/envelopes/symphony_gain.h"
#include "nodes/envelopes/symphony_adsr.h"
#include "nodes/envelopes/symphony_compressor.h"
#include "nodes/envelopes/symphony_envelope_float.h"
#include "nodes/math/symphony_math_add.h"
#include "nodes/math/symphony_mix.h"
#include "nodes/math/symphony_map_range.h"
#include "nodes/math/symphony_sample_hold.h"
#include "nodes/timing/symphony_clock.h"
#include "nodes/timing/symphony_trigger_delay.h"
#include "nodes/timing/symphony_stochastic_trigger.h"
#include "nodes/delay/symphony_delay_line.h"
#include "nodes/delay/symphony_feedback_path.h"
#include "nodes/delay/symphony_pitch_shifter.h"
#include "nodes/utility/symphony_parameter_smoother.h"
#include "nodes/utility/symphony_envelope_follower.h"
#include "nodes/utility/symphony_frequency_envelope_follower.h"
#include "nodes/synthesis/symphony_modal_bank.h"
#include "nodes/synthesis/symphony_grain_cloud.h"
#include "nodes/math/symphony_ring_mod.h"
#include "nodes/math/symphony_crossfade.h"
#include "nodes/generators/symphony_fm_oscillator.h"
#include "nodes/filters/symphony_waveshaper.h"
#include "nodes/generators/symphony_formant_osc.h"
#include "nodes/delay/symphony_fdn_reverb.h"
#include "nodes/spectral/symphony_phase_vocoder.h"
#include "nodes/spectral/symphony_spectral_gate.h"
#include "nodes/spectral/symphony_resonator_analyzer.h"
#include "nodes/io/symphony_graph_input.h"
#include "nodes/io/symphony_graph_input_audio.h"
#include "nodes/io/symphony_graph_output.h"
#include "nodes/io/symphony_trigger_input.h"
#include "nodes/io/symphony_subgraph.h"

#include "core/object/class_db.h"
#include "core/config/engine.h"

#ifdef TOOLS_ENABLED
#include "editor/symphony_editor_plugin.h"
#endif

void initialize_symphony_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// Create shared PCM cache (used by GrainCloud for shared source buffers)
		memnew(SharedPCMCache);

		// Create operator registry and register all built-in operators
		OperatorRegistry::create_singleton();

		// Generators
		SymphonyOscillator::register_operator();
		SymphonyConstant::register_operator();
		SymphonyNoise::register_operator();
		SymphonyLFO::register_operator();
		SymphonyWavePlayer::register_operator();

		// Filters
		SymphonyBiquadFilter::register_operator();
		SymphonyOnePole::register_operator();
		SymphonyDCBlocker::register_operator();
		SymphonySaturator::register_operator();
		SymphonySVFilter::register_operator();

		// Envelopes & Dynamics
		SymphonyGain::register_operator();
		SymphonyADSR::register_operator();
		SymphonyCompressor::register_operator();
		SymphonyEnvelopeFloat::register_operator();

		// Math
		SymphonyMathAdd::register_operator();
		SymphonyMix::register_operator();
		SymphonyMapRange::register_operator();
		SymphonySampleHold::register_operator();

		// Timing
		SymphonyClock::register_operator();
		SymphonyTriggerDelay::register_operator();
		SymphonyStochasticTrigger::register_operator();

		// Delay
		SymphonyDelayLine::register_operator();
		SymphonyFeedbackPath::register_operator();
		SymphonyPitchShifter::register_operator();

		// Utility
		SymphonyParameterSmoother::register_operator();
		SymphonyEnvelopeFollower::register_operator();
		SymphonyFrequencyEnvelopeFollower::register_operator();

		// Synthesis
		SymphonyModalBank::register_operator();
		SymphonyGrainCloud::register_operator();

		// S2 Math
		SymphonyRingMod::register_operator();
		SymphonyCrossFade::register_operator();

		// S2 Generators
		SymphonyFMOscillator::register_operator();

		// S2 Filters
		SymphonyWaveshaper::register_operator();

		// S3 Generators
		SymphonyFormantOsc::register_operator();

		// S3 Delay
		SymphonyFDNReverb::register_operator();

		// S4 Spectral
		SymphonyPhaseVocoder::register_operator();
		SymphonySpectralGate::register_operator();
		SymphonyResonatorAnalyzer::register_operator();

		// I/O
		SymphonyGraphOutput::register_operator();
		SymphonyGraphInput::register_operator();
		SymphonyGraphInputAudio::register_operator();
		SymphonyTriggerInput::register_operator();
		SymphonySubGraph::register_operator();

		// Register Godot classes
		GDREGISTER_CLASS(AudioStreamSymphony);
		GDREGISTER_CLASS(AudioStreamPlaybackSymphony);
		GDREGISTER_CLASS(SymphonyVoiceManager);
		GDREGISTER_CLASS(SoundEvent);
		GDREGISTER_CLASS(MusicStateGraph);
		GDREGISTER_CLASS(BeatClock);
		GDREGISTER_CLASS(SymphonyVoicePool);
		GDREGISTER_CLASS(SymphonyEventDispatcher);
		GDREGISTER_CLASS(RTPCEngine);
		GDREGISTER_CLASS(BusController);
		GDREGISTER_CLASS(TransitionAnalyzer);

		// Create voice manager singleton (DSP graph tracking)
		memnew(SymphonyVoiceManager);
		Engine::get_singleton()->add_singleton(Engine::Singleton("SymphonyVoiceManager", SymphonyVoiceManager::get_singleton(), "SymphonyVoiceManager"));

		// Create voice pool singleton (game-level voice management)
		memnew(SymphonyVoicePool);
		Engine::get_singleton()->add_singleton(Engine::Singleton("SymphonyVoicePool", SymphonyVoicePool::get_singleton(), "SymphonyVoicePool"));

		// Create event dispatcher singleton
		memnew(SymphonyEventDispatcher);
		Engine::get_singleton()->add_singleton(Engine::Singleton("SymphonyEventDispatcher", SymphonyEventDispatcher::get_singleton(), "SymphonyEventDispatcher"));

		// Create beat clock singleton
		memnew(BeatClock);
		Engine::get_singleton()->add_singleton(Engine::Singleton("BeatClock", BeatClock::get_singleton(), "BeatClock"));

		// Create RTPC engine singleton
		memnew(RTPCEngine);
		Engine::get_singleton()->add_singleton(Engine::Singleton("RTPCEngine", RTPCEngine::get_singleton(), "RTPCEngine"));

		// Create bus controller singleton
		memnew(BusController);
		Engine::get_singleton()->add_singleton(Engine::Singleton("BusController", BusController::get_singleton(), "BusController"));

		// Create transition analyzer singleton
		memnew(TransitionAnalyzer);
		Engine::get_singleton()->add_singleton(Engine::Singleton("TransitionAnalyzer", TransitionAnalyzer::get_singleton(), "TransitionAnalyzer"));
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
		if (BeatClock::get_singleton()) {
			Engine::get_singleton()->remove_singleton("BeatClock");
			memdelete(BeatClock::get_singleton());
		}
		if (BusController::get_singleton()) {
			Engine::get_singleton()->remove_singleton("BusController");
			memdelete(BusController::get_singleton());
		}
		if (TransitionAnalyzer::get_singleton()) {
			Engine::get_singleton()->remove_singleton("TransitionAnalyzer");
			memdelete(TransitionAnalyzer::get_singleton());
		}
		if (RTPCEngine::get_singleton()) {
			Engine::get_singleton()->remove_singleton("RTPCEngine");
			memdelete(RTPCEngine::get_singleton());
		}
		if (SymphonyEventDispatcher::get_singleton()) {
			Engine::get_singleton()->remove_singleton("SymphonyEventDispatcher");
			memdelete(SymphonyEventDispatcher::get_singleton());
		}
		if (SymphonyVoicePool::get_singleton()) {
			Engine::get_singleton()->remove_singleton("SymphonyVoicePool");
			memdelete(SymphonyVoicePool::get_singleton());
		}
		if (SymphonyVoiceManager::get_singleton()) {
			Engine::get_singleton()->remove_singleton("SymphonyVoiceManager");
			memdelete(SymphonyVoiceManager::get_singleton());
		}
		if (SharedPCMCache::get_singleton()) {
			memdelete(SharedPCMCache::get_singleton());
		}
		OperatorRegistry::destroy_singleton();
	}
}
