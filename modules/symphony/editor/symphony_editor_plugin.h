#pragma once

#include "editor/plugins/editor_plugin.h"
#include "editor/editor_undo_redo_manager.h"
#include "scene/gui/graph_edit.h"
#include "scene/gui/graph_node.h"
#include "scene/gui/graph_frame.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/option_button.h"
#include "scene/gui/file_dialog.h"
#include "scene/audio/audio_stream_player.h"
#include "core/object/callable_mp.h"

#include "../stream/audio_stream_symphony.h"
#include "../stream/audio_stream_playback_symphony.h"
#include "../core/symphony_operator_registry.h"

class SymphonyGraphEditor;

// Proxy object that exposes a selected Symphony node's params in the Inspector.
class SymphonyNodeInspectorProxy : public Object {
	GDCLASS(SymphonyNodeInspectorProxy, Object)

private:
	SymphonyGraphEditor *editor = nullptr;
	int32_t node_id = -1;
	StringName type_name;

protected:
	static void _bind_methods();
	void _get_property_list(List<PropertyInfo> *p_list) const;
	bool _get(const StringName &p_name, Variant &r_ret) const;
	bool _set(const StringName &p_name, const Variant &p_value);

public:
	void setup(SymphonyGraphEditor *p_editor, int32_t p_node_id, const StringName &p_type_name);
	int32_t get_node_id() const { return node_id; }
};

class SymphonyGraphEditor : public VBoxContainer {
	GDCLASS(SymphonyGraphEditor, VBoxContainer)

	friend class SymphonyNodeInspectorProxy;

private:
	Ref<AudioStreamSymphony> stream;
	GraphEdit *graph_edit = nullptr;
	MenuButton *add_node_menu = nullptr;
	Button *preview_button = nullptr;
	Button *save_button = nullptr;
	Button *add_frame_button = nullptr;
	AudioStreamPlayer *preview_player = nullptr;
	bool previewing = false;

	int32_t next_node_id = 0;
	int32_t next_frame_id = 0;

	SymphonyNodeInspectorProxy *inspector_proxy = nullptr;

	// SubGraph support.
	FileDialog *file_dialog = nullptr;
	int32_t pending_subgraph_node_id = -1; // ID reserved for the SubGraph node being created via file picker
	int32_t pending_resource_node_id = -1; // ID of node whose resource_path is being picked
	static constexpr int SUBGRAPH_MENU_ID = 9999;
	static constexpr int CREATE_SUBGRAPH_MENU_ID = 9998;

	// Breadcrumb navigation stack.
	struct NavEntry {
		Ref<AudioStreamSymphony> resource;
		Vector2 scroll_offset;
	};
	Vector<NavEntry> nav_stack; // Stack of parent graphs (current is always `stream`)
	HBoxContainer *breadcrumb_bar = nullptr;

	void _navigate_to(int p_depth); // Navigate to a specific breadcrumb level
	void _push_subgraph(const String &p_resource_path);
	void _rebuild_breadcrumbs();
	void _on_node_gui_input(const Ref<InputEvent> &p_event, int32_t p_node_id);

	// Clipboard for copy/paste (static for cross-graph support).
	struct ClipboardData {
		Vector<NodeDesc> nodes;
		Vector<ConnectionDesc> connections;
		Vector2 center;
	};
	static ClipboardData clipboard;

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
	void _on_delete_pressed();
	void _on_param_changed(double p_value, int32_t p_node_id, const StringName &p_param_name);
	void _on_collapse_toggled(bool p_collapsed, int32_t p_node_id);
	void _set_params_visible(GraphNode *p_node, bool p_visible);

	// SubGraph support.
	void _on_file_dialog_file_selected(const String &p_path);
	void _on_resource_file_selected(const String &p_path);
	void _on_resource_pick_pressed(int32_t p_node_id);
	void _on_create_new_subgraph();
	GraphNode *_create_subgraph_node(const NodeDesc &p_node);

	// Pin type dropdown.
	void _on_pin_type_changed(int p_index, int32_t p_node_id);

	// Comment frames.
	void _on_add_frame_pressed();
	void _create_frame_ui(const FrameDesc &p_frame);

	// Copy/Paste/Duplicate.
	void _on_copy_nodes_request();
	void _on_paste_nodes_request();
	void _on_duplicate_nodes_request();

	// Inspector.
	void _on_node_selected(Node *p_node);

	// Undo/Redo helper methods (called by EditorUndoRedoManager).
	void _ur_add_node(const Dictionary &p_node_data);
	void _ur_remove_node(int32_t p_node_id);
	void _ur_connect(int32_t p_from, int p_from_pin, int32_t p_to, int p_to_pin);
	void _ur_disconnect(int32_t p_from, int p_from_pin, int32_t p_to, int p_to_pin);
	void _ur_set_param(int32_t p_node_id, const StringName &p_param_name, float p_value);
	void _ur_set_node_position(int32_t p_node_id, const Vector2 &p_position);
	void _ur_add_frame(const Dictionary &p_frame_data);
	void _ur_remove_frame(int32_t p_frame_id);

	void _sync_graph_description();
	void _recompile_and_preview();

	int32_t _node_id_from_name(const StringName &p_name) const;
	StringName _name_from_node_id(int32_t p_id) const;
	StringName _frame_name_from_id(int32_t p_id) const;
	int32_t _frame_id_from_name(const StringName &p_name) const;

	Dictionary _node_desc_to_dict(const NodeDesc &p_node) const;
	NodeDesc _dict_to_node_desc(const Dictionary &p_dict) const;

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
