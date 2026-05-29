#pragma once

#include "editor/plugins/editor_plugin.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/spin_box.h"
#include "scene/audio/audio_stream_player.h"
#include "core/object/callable_mp.h"

#include "../stream/audio_stream_symphony.h"
#include "../stream/audio_stream_playback_symphony.h"
#include "../core/symphony_operator_registry.h"

class SymphonyGraphEditor : public VBoxContainer {
	GDCLASS(SymphonyGraphEditor, VBoxContainer)

private:
	Ref<AudioStreamSymphony> stream;
	GraphEdit *graph_edit = nullptr;
	MenuButton *add_node_menu = nullptr;
	Button *preview_button = nullptr;
	Button *save_button = nullptr;
	AudioStreamPlayer *preview_player = nullptr;
	bool previewing = false;

	int32_t next_node_id = 0;

	// Pin type constants for GraphEdit type validation.
	enum PinTypeSlot {
		SLOT_AUDIO = 0,
		SLOT_FLOAT = 1,
		SLOT_INT = 2,
		SLOT_BOOL = 3,
		SLOT_TRIGGER = 4,
	};

	Color get_pin_color(SymphonyPinType p_type) const;
	int get_slot_type(SymphonyPinType p_type) const;

	void _rebuild_graph_edit();
	GraphNode *_create_graph_node(const NodeDesc &p_node);
	void _populate_add_menu();

	void _on_add_node_id_pressed(int p_id);
	void _on_connection_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port);
	void _on_disconnection_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port);
	void _on_delete_nodes_request(const TypedArray<StringName> &p_nodes);
	void _on_node_position_changed(const StringName &p_node);
	void _on_preview_pressed();
	void _on_save_pressed();
	void _on_param_changed(double p_value, int32_t p_node_id, const StringName &p_param_name);

	void _sync_graph_description();
	void _recompile_and_preview();

	int32_t _node_id_from_name(const StringName &p_name) const;
	StringName _name_from_node_id(int32_t p_id) const;

protected:
	static void _bind_methods();

public:
	void edit(Ref<AudioStreamSymphony> p_stream);
	Ref<AudioStreamSymphony> get_edited_stream() const { return stream; }

	SymphonyGraphEditor();
	~SymphonyGraphEditor();
};

class SymphonyEditorPlugin : public EditorPlugin {
	GDCLASS(SymphonyEditorPlugin, EditorPlugin)

private:
	SymphonyGraphEditor *graph_editor = nullptr;
	Button *bottom_panel_button = nullptr;

protected:
	static void _bind_methods();

public:
	virtual String get_plugin_name() const override { return "Symphony"; }
	virtual bool handles(Object *p_object) const override;
	virtual void edit(Object *p_object) override;
	virtual void make_visible(bool p_visible) override;

	SymphonyEditorPlugin();
	~SymphonyEditorPlugin();
};
