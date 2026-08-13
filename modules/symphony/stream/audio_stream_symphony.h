#pragma once

#include "scene/resources/audio/audio_stream.h"
#include "../core/symphony_graph_description.h"
#include "../core/symphony_compiled_graph.h"

class AudioStreamPlaybackSymphony;

class AudioStreamSymphony : public AudioStream {
	GDCLASS(AudioStreamSymphony, AudioStream)

private:
	friend class AudioStreamPlaybackSymphony;
	float mix_rate = 44100.0f;
	int voice_priority = 50; // 0-100, higher = harder to steal
	GraphDescription graph_desc; // LOD 0 (full quality)

	// LOD system: simplified graph variants for distance/importance-based degradation.
	// lod_graphs[0] = LOD 1 (simplified), lod_graphs[1] = LOD 2 (minimal).
	// Main graph_desc is always LOD 0 (full quality).
	Vector<GraphDescription> lod_graphs;

	// LOD distance thresholds (normalized 0-1 of max_distance).
	// Default: LOD 0 at 0-30%, LOD 1 at 30-70%, LOD 2 at 70-100%.
	float lod_threshold_1 = 0.3f; // Distance ratio above which LOD 1 activates
	float lod_threshold_2 = 0.7f; // Distance ratio above which LOD 2 activates

protected:
	static void _bind_methods();

	// Resource serialization for .tres/.res
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _get(const StringName &p_name, Variant &r_ret) const;
	bool _set(const StringName &p_name, const Variant &p_value);

	static void _append_graph_properties(List<PropertyInfo> *p_list, const String &p_prefix, const GraphDescription &p_desc);
	static bool _get_graph_property(const String &p_relative, const GraphDescription &p_desc, Variant &r_ret);
	static bool _set_graph_property(const String &p_relative, GraphDescription &p_desc, const Variant &p_value);

public:
	void set_mix_rate(float p_mix_rate);
	float get_mix_rate() const;

	void set_voice_priority(int p_priority);
	int get_voice_priority() const;

	void set_graph_description(const GraphDescription &p_desc);
	const GraphDescription &get_graph_description() const;

	// Tier 0 = main graph; tiers 1..2 = lod_graphs[0..1].
	const GraphDescription &get_graph_for_tier(int p_tier) const;
	GraphDescription &get_graph_for_tier_mut(int p_tier);
	void set_graph_for_tier(int p_tier, const GraphDescription &p_desc);

	CompiledGraph *compile_graph() const;
	CompiledGraph *compile_lod_graph(int p_lod_tier) const;
	[[nodiscard]] int get_lod_count() const; // Returns 1 if no LOD graphs, up to 3 (LOD 0 + 2 variants)
	[[nodiscard]] int get_lod_variant_count() const { return lod_graphs.size(); }

	// LOD authoring APIs (plan §11). Variants are tiers 1 and 2 only.
	int add_lod_variant(); // empty variant; returns tier or -1 if full
	int duplicate_main_to_lod(); // copy main graph into a new variant
	bool set_lod_variant(int p_tier, const GraphDescription &p_desc); // tier 1 or 2
	bool remove_lod_variant(int p_tier); // tier 1 or 2; renumbers remaining
	GraphDescription get_lod_variant(int p_tier) const;
	bool has_lod_variant(int p_tier) const;

	void set_lod_threshold_1(float p_threshold);
	[[nodiscard]] float get_lod_threshold_1() const;
	void set_lod_threshold_2(float p_threshold);
	[[nodiscard]] float get_lod_threshold_2() const;

	[[nodiscard]] int get_recommended_lod(float p_distance_ratio) const;

	// Per-tier memory estimates at supported sample rates (editor / diagnostics).
	Dictionary estimate_tier_memory(int p_tier) const;
	Dictionary validate_tier_compile(int p_tier) const;

	static GraphDescription build_test_graph_10_nodes();
	static GraphDescription build_test_graph_30_nodes();
	static GraphDescription build_test_graph_50_nodes();
	void load_test_graph();
	void load_test_graph_30();
	void load_test_graph_50();

	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual double get_length() const override;
	virtual bool is_monophonic() const override;
};
