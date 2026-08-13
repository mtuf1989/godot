/**************************************************************************/
/*  test_symphony_serialization.cpp                                       */
/*  Suite: [Symphony][Serialization] — LOD/feedback .tres round trips.    */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_symphony_serialization)

#include "modules/symphony/stream/audio_stream_symphony.h"
#include "modules/symphony/core/symphony_graph_description.h"
#include "core/io/resource.h"
#include "core/variant/dictionary.h"

namespace TestSymphonySerialization {

static GraphDescription _make_simple_graph(bool p_feedback) {
	GraphDescription desc;
	NodeDesc osc;
	osc.id = 1;
	osc.type_name = "Oscillator";
	osc.params.insert("frequency", 440.0f);
	osc.editor_position = Vector2(10, 20);
	desc.nodes.push_back(osc);

	NodeDesc out;
	out.id = 2;
	out.type_name = "GraphOutput";
	out.editor_position = Vector2(200, 20);
	desc.nodes.push_back(out);

	ConnectionDesc c;
	c.from_node = 1;
	c.from_pin = 0;
	c.to_node = 2;
	c.to_pin = 0;
	c.is_feedback = p_feedback;
	desc.connections.push_back(c);

	desc.smooth_parameters = false;
	desc.anti_alias_staircase = true;
	desc.smooth_time_ms = 7.5f;
	return desc;
}

static void _copy_storage_props(const AudioStreamSymphony *p_src, AudioStreamSymphony *p_dst) {
	List<PropertyInfo> props;
	p_src->get_property_list(&props);
	for (const PropertyInfo &pi : props) {
		if (!(pi.usage & PROPERTY_USAGE_STORAGE)) {
			continue;
		}
		bool valid = false;
		Variant value = p_src->get(pi.name, &valid);
		if (valid) {
			p_dst->set(pi.name, value);
		}
	}
}

TEST_CASE("[Symphony][Serialization] is_feedback and LOD sections round-trip") {
	Ref<AudioStreamSymphony> src;
	src.instantiate();
	src->set_graph_description(_make_simple_graph(true));
	CHECK(src->duplicate_main_to_lod() == 1);
	GraphDescription lod2 = _make_simple_graph(false);
	lod2.nodes.write[0].params["frequency"] = 220.0f;
	CHECK(src->set_lod_variant(2, lod2));
	CHECK(src->get_lod_variant_count() == 2);
	CHECK(src->has_lod_variant(1));
	CHECK(src->get_lod_variant(1).connections[0].is_feedback == true);

	Ref<AudioStreamSymphony> dst;
	dst.instantiate();
	_copy_storage_props(src.ptr(), dst.ptr());

	CHECK(dst->get_graph_description().connections.size() == 1);
	CHECK(dst->get_graph_description().connections[0].is_feedback == true);
	CHECK(dst->get_graph_description().smooth_parameters == false);
	CHECK(dst->get_graph_description().anti_alias_staircase == true);
	CHECK(dst->get_graph_description().smooth_time_ms == doctest::Approx(7.5f));

	CHECK(dst->get_lod_variant_count() == 2);
	CHECK(dst->get_lod_variant(1).nodes.size() == 2);
	CHECK(dst->get_lod_variant(1).connections[0].is_feedback == true);
	CHECK((float)dst->get_lod_variant(2).nodes[0].params["frequency"] == doctest::Approx(220.0f));
}

TEST_CASE("[Symphony][Serialization] LOD mutation APIs and memory estimate") {
	Ref<AudioStreamSymphony> stream;
	stream.instantiate();
	stream->set_graph_description(_make_simple_graph(false));

	CHECK(stream->add_lod_variant() == 1);
	CHECK(stream->duplicate_main_to_lod() == 2);
	CHECK(stream->add_lod_variant() == -1);
	CHECK(stream->remove_lod_variant(1));
	CHECK(stream->get_lod_variant_count() == 1);
	CHECK(stream->has_lod_variant(1));
	CHECK_FALSE(stream->has_lod_variant(2));

	Dictionary mem = stream->estimate_tier_memory(0);
	CHECK((bool)mem.get("ok", false) == true);
	CHECK((int64_t)mem.get("48000", -1) > 0);

	Dictionary valid = stream->validate_tier_compile(0);
	CHECK((bool)valid.get("ok", false) == true);
}

} // namespace TestSymphonySerialization
