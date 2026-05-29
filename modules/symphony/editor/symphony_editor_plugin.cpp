#include "symphony_editor_plugin.h"

#include "core/io/resource_saver.h"
#include "editor/editor_node.h"
#include "scene/gui/label.h"

// --- SymphonyGraphEditor ---

void SymphonyGraphEditor::_bind_methods() {
}

SymphonyGraphEditor::SymphonyGraphEditor() {
	// Toolbar
	HBoxContainer *toolbar = memnew(HBoxContainer);
	add_child(toolbar);

	add_node_menu = memnew(MenuButton);
	add_node_menu->set_text("Add Node");
	add_node_menu->set_switch_on_hover(true);
	toolbar->add_child(add_node_menu);
	_populate_add_menu();
	add_node_menu->get_popup()->connect("id_pressed", callable_mp(this, &SymphonyGraphEditor::_on_add_node_id_pressed));

	preview_button = memnew(Button);
	preview_button->set_text("Preview");
	preview_button->set_toggle_mode(true);
	preview_button->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_on_preview_pressed));
	toolbar->add_child(preview_button);

	save_button = memnew(Button);
	save_button->set_text("Save");
	save_button->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_on_save_pressed));
	toolbar->add_child(save_button);

	// GraphEdit
	graph_edit = memnew(GraphEdit);
	graph_edit->set_v_size_flags(SIZE_EXPAND_FILL);
	graph_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	graph_edit->set_custom_minimum_size(Vector2(0, 300));
	add_child(graph_edit);

	// Register valid connection types (same type + Float→Audio promotion).
	for (int i = 0; i <= SLOT_TRIGGER; i++) {
		graph_edit->add_valid_connection_type(i, i);
	}
	graph_edit->add_valid_connection_type(SLOT_FLOAT, SLOT_AUDIO); // Float→Audio promotion

	graph_edit->connect("connection_request", callable_mp(this, &SymphonyGraphEditor::_on_connection_request));
	graph_edit->connect("disconnection_request", callable_mp(this, &SymphonyGraphEditor::_on_disconnection_request));
	graph_edit->connect("delete_nodes_request", callable_mp(this, &SymphonyGraphEditor::_on_delete_nodes_request));

	// Preview player (hidden)
	preview_player = memnew(AudioStreamPlayer);
	add_child(preview_player);
}

SymphonyGraphEditor::~SymphonyGraphEditor() {
}

Color SymphonyGraphEditor::get_pin_color(SymphonyPinType p_type) const {
	switch (p_type) {
		case SymphonyPinType::AUDIO:
			return Color(0.4, 0.6, 1.0); // Blue
		case SymphonyPinType::FLOAT:
			return Color(0.4, 0.9, 0.4); // Green
		case SymphonyPinType::INT:
			return Color(0.9, 0.9, 0.3); // Yellow
		case SymphonyPinType::BOOL:
			return Color(0.9, 0.9, 0.9); // White
		case SymphonyPinType::TRIGGER:
			return Color(1.0, 0.6, 0.2); // Orange
	}
	return Color(1, 1, 1);
}

int SymphonyGraphEditor::get_slot_type(SymphonyPinType p_type) const {
	return (int)p_type;
}

void SymphonyGraphEditor::_populate_add_menu() {
	PopupMenu *popup = add_node_menu->get_popup();
	popup->clear();

	const OperatorRegistry *reg = OperatorRegistry::get_singleton();
	if (!reg) {
		return;
	}

	Vector<StringName> types;
	reg->get_registered_types(types);

	// Group by category.
	HashMap<String, Vector<int>> categories;
	for (int i = 0; i < types.size(); i++) {
		const OperatorDescriptor *desc = reg->find(types[i]);
		String cat = desc && !desc->category.is_empty() ? desc->category : "Other";
		if (!categories.has(cat)) {
			categories[cat] = Vector<int>();
		}
		categories[cat].push_back(i);
	}

	// Create sub-menus for each category.
	for (const KeyValue<String, Vector<int>> &kv : categories) {
		PopupMenu *sub = memnew(PopupMenu);
		sub->set_name(kv.key);
		for (int idx : kv.value) {
			sub->add_item(String(types[idx]), idx);
		}
		sub->connect("id_pressed", callable_mp(this, &SymphonyGraphEditor::_on_add_node_id_pressed));
		popup->add_child(sub);
		popup->add_submenu_item(kv.key, kv.key);
	}
}

void SymphonyGraphEditor::edit(Ref<AudioStreamSymphony> p_stream) {
	if (stream == p_stream) {
		return;
	}
	stream = p_stream;

	// Determine next_node_id from existing nodes.
	next_node_id = 0;
	if (stream.is_valid()) {
		const GraphDescription &desc = stream->get_graph_description();
		for (int i = 0; i < desc.nodes.size(); i++) {
			if (desc.nodes[i].id >= next_node_id) {
				next_node_id = desc.nodes[i].id + 1;
			}
		}
	}

	_rebuild_graph_edit();
}

void SymphonyGraphEditor::_rebuild_graph_edit() {
	graph_edit->clear_connections();
	// Remove all existing GraphNodes.
	Vector<Node *> to_remove;
	for (int i = 0; i < graph_edit->get_child_count(); i++) {
		GraphNode *gn = Object::cast_to<GraphNode>(graph_edit->get_child(i));
		if (gn) {
			to_remove.push_back(gn);
		}
	}
	for (Node *n : to_remove) {
		graph_edit->remove_child(n);
		n->queue_free();
	}

	if (!stream.is_valid()) {
		return;
	}

	const GraphDescription &desc = stream->get_graph_description();

	// Create nodes.
	for (int i = 0; i < desc.nodes.size(); i++) {
		GraphNode *gn = _create_graph_node(desc.nodes[i]);
		graph_edit->add_child(gn);
	}

	// Create connections.
	for (int i = 0; i < desc.connections.size(); i++) {
		const ConnectionDesc &conn = desc.connections[i];
		StringName from_name = _name_from_node_id(conn.from_node);
		StringName to_name = _name_from_node_id(conn.to_node);
		graph_edit->connect_node(from_name, conn.from_pin, to_name, conn.to_pin);
	}
}

GraphNode *SymphonyGraphEditor::_create_graph_node(const NodeDesc &p_node) {
	GraphNode *gn = memnew(GraphNode);
	gn->set_name(_name_from_node_id(p_node.id));
	gn->set_title(String(p_node.type_name));
	gn->set_position_offset(p_node.editor_position);

	// Look up the operator descriptor for pin info.
	const OperatorRegistry *reg = OperatorRegistry::get_singleton();
	const OperatorDescriptor *op_desc = reg ? reg->find(p_node.type_name) : nullptr;

	if (!op_desc) {
		Label *lbl = memnew(Label);
		lbl->set_text("Unknown: " + String(p_node.type_name));
		gn->add_child(lbl);
		return gn;
	}

	// Pin slots.
	int max_slots = MAX(op_desc->inputs.size(), op_desc->outputs.size());
	for (int i = 0; i < max_slots; i++) {
		Label *lbl = memnew(Label);
		String text;
		bool has_left = i < op_desc->inputs.size();
		bool has_right = i < op_desc->outputs.size();

		if (has_left) {
			text = String(op_desc->inputs[i].name);
		}
		if (has_right) {
			if (!text.is_empty()) {
				text += "  →  ";
			}
			text += String(op_desc->outputs[i].name);
		}
		lbl->set_text(text);
		gn->add_child(lbl);

		int left_type = has_left ? get_slot_type(op_desc->inputs[i].type) : 0;
		Color left_color = has_left ? get_pin_color(op_desc->inputs[i].type) : Color(1, 1, 1);
		int right_type = has_right ? get_slot_type(op_desc->outputs[i].type) : 0;
		Color right_color = has_right ? get_pin_color(op_desc->outputs[i].type) : Color(1, 1, 1);

		gn->set_slot(i, has_left, left_type, left_color, has_right, right_type, right_color);
	}

	// Parameter editors (SpinBox for each param).
	for (int i = 0; i < op_desc->params.size(); i++) {
		const ParamDescriptor &pd = op_desc->params[i];

		HBoxContainer *row = memnew(HBoxContainer);
		Label *lbl = memnew(Label);
		lbl->set_text(String(pd.name) + ":");
		lbl->set_custom_minimum_size(Vector2(60, 0));
		row->add_child(lbl);

		SpinBox *spin = memnew(SpinBox);
		spin->set_min(pd.min_value);
		spin->set_max(pd.max_value);
		spin->set_step(pd.step);
		spin->set_custom_minimum_size(Vector2(80, 0));

		// Set current value from node params, or use default.
		float val = pd.default_value;
		if (p_node.params.has(pd.name)) {
			val = p_node.params[pd.name];
		}
		spin->set_value(val);

		spin->connect("value_changed", callable_mp(this, &SymphonyGraphEditor::_on_param_changed).bind(p_node.id, pd.name));
		row->add_child(spin);
		gn->add_child(row);

		// These rows don't have slots (no pins).
		gn->set_slot(max_slots + i, false, 0, Color(), false, 0, Color());
	}

	gn->connect("position_offset_changed", callable_mp(this, &SymphonyGraphEditor::_on_node_position_changed).bind(gn->get_name()));

	return gn;
}

void SymphonyGraphEditor::_on_add_node_id_pressed(int p_id) {
	if (!stream.is_valid()) {
		return;
	}

	const OperatorRegistry *reg = OperatorRegistry::get_singleton();
	Vector<StringName> types;
	reg->get_registered_types(types);
	if (p_id < 0 || p_id >= types.size()) {
		return;
	}

	NodeDesc nd;
	nd.id = next_node_id++;
	nd.type_name = types[p_id];
	nd.editor_position = graph_edit->get_scroll_offset() + Vector2(100, 100);

	// Populate default params from the operator descriptor.
	const OperatorDescriptor *op_desc = reg->find(nd.type_name);
	if (op_desc) {
		for (int i = 0; i < op_desc->params.size(); i++) {
			nd.params[op_desc->params[i].name] = op_desc->params[i].default_value;
		}
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	desc.nodes.push_back(nd);

	GraphNode *gn = _create_graph_node(nd);
	graph_edit->add_child(gn);

	stream->notify_property_list_changed();
	stream->emit_changed();
}

void SymphonyGraphEditor::_on_connection_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port) {
	if (!stream.is_valid()) {
		return;
	}

	graph_edit->connect_node(p_from, p_from_port, p_to, p_to_port);
	_sync_graph_description();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_on_disconnection_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port) {
	if (!stream.is_valid()) {
		return;
	}

	graph_edit->disconnect_node(p_from, p_from_port, p_to, p_to_port);
	_sync_graph_description();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_on_delete_nodes_request(const TypedArray<StringName> &p_nodes) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());

	for (int i = 0; i < p_nodes.size(); i++) {
		StringName node_name = p_nodes[i];
		int32_t node_id = _node_id_from_name(node_name);

		// Remove connections involving this node.
		for (int c = desc.connections.size() - 1; c >= 0; c--) {
			if (desc.connections[c].from_node == node_id || desc.connections[c].to_node == node_id) {
				desc.connections.remove_at(c);
			}
		}

		// Remove the node.
		for (int n = 0; n < desc.nodes.size(); n++) {
			if (desc.nodes[n].id == node_id) {
				desc.nodes.remove_at(n);
				break;
			}
		}

		// Remove from GraphEdit.
		Node *child = graph_edit->get_node_or_null(NodePath(String(node_name)));
		if (child) {
			graph_edit->remove_child(child);
			child->queue_free();
		}
	}

	stream->notify_property_list_changed();
	stream->emit_changed();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_on_node_position_changed(const StringName &p_node) {
	if (!stream.is_valid()) {
		return;
	}

	int32_t node_id = _node_id_from_name(p_node);
	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());

	Node *child = graph_edit->get_node_or_null(NodePath(String(p_node)));
	GraphNode *gn = Object::cast_to<GraphNode>(child);
	if (!gn) {
		return;
	}

	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == node_id) {
			desc.nodes.write[i].editor_position = gn->get_position_offset();
			break;
		}
	}

	stream->emit_changed();
}

void SymphonyGraphEditor::_on_param_changed(double p_value, int32_t p_node_id, const StringName &p_param_name) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == p_node_id) {
			desc.nodes.write[i].params[p_param_name] = (float)p_value;
			break;
		}
	}

	stream->emit_changed();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_on_save_pressed() {
	if (!stream.is_valid()) {
		return;
	}

	_sync_graph_description();

	String path = stream->get_path();
	if (path.is_empty()) {
		// Resource has never been saved — open save dialog.
		EditorNode::get_singleton()->save_resource_as(stream);
	} else {
		ResourceSaver::save(stream, path);
	}
}

void SymphonyGraphEditor::_on_preview_pressed() {
	previewing = preview_button->is_pressed();
	if (previewing) {
		_sync_graph_description();
		_recompile_and_preview();
	} else {
		preview_player->stop();
	}
}

void SymphonyGraphEditor::_sync_graph_description() {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());

	// Rebuild connections from GraphEdit state.
	desc.connections.clear();
	const Vector<Ref<GraphEdit::Connection>> &connections = graph_edit->get_connections();

	for (const Ref<GraphEdit::Connection> &c : connections) {
		ConnectionDesc cd;
		cd.from_node = _node_id_from_name(c->from_node);
		cd.from_pin = c->from_port;
		cd.to_node = _node_id_from_name(c->to_node);
		cd.to_pin = c->to_port;
		if (cd.from_node >= 0 && cd.to_node >= 0) {
			desc.connections.push_back(cd);
		}
	}

	stream->notify_property_list_changed();
	stream->emit_changed();
}

void SymphonyGraphEditor::_recompile_and_preview() {
	if (!previewing || !stream.is_valid()) {
		return;
	}

	// If already playing, hot-swap via the playback.
	if (preview_player->is_playing()) {
		Ref<AudioStreamPlayback> base_playback = preview_player->get_stream_playback();
		AudioStreamPlaybackSymphony *playback = Object::cast_to<AudioStreamPlaybackSymphony>(base_playback.ptr());
		if (playback) {
			CompiledGraph *compiled = stream->compile_graph();
			if (compiled) {
				playback->swap_graph(compiled);
			}
			return;
		}
	}

	// Start fresh playback.
	preview_player->set_stream(stream);
	preview_player->play();
}

int32_t SymphonyGraphEditor::_node_id_from_name(const StringName &p_name) const {
	// Node names are "node_<id>"
	String s = String(p_name);
	if (s.begins_with("node_")) {
		return s.substr(5).to_int();
	}
	return -1;
}

StringName SymphonyGraphEditor::_name_from_node_id(int32_t p_id) const {
	return StringName(vformat("node_%d", p_id));
}

// --- SymphonyEditorPlugin ---

void SymphonyEditorPlugin::_bind_methods() {
}

SymphonyEditorPlugin::SymphonyEditorPlugin() {
	graph_editor = memnew(SymphonyGraphEditor);
	graph_editor->set_custom_minimum_size(Vector2(0, 300));
	bottom_panel_button = add_control_to_bottom_panel(graph_editor, "Symphony");
	bottom_panel_button->hide();
}

SymphonyEditorPlugin::~SymphonyEditorPlugin() {
}

bool SymphonyEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<AudioStreamSymphony>(p_object) != nullptr;
}

void SymphonyEditorPlugin::edit(Object *p_object) {
	AudioStreamSymphony *s = Object::cast_to<AudioStreamSymphony>(p_object);
	if (s) {
		graph_editor->edit(Ref<AudioStreamSymphony>(s));
	}
}

void SymphonyEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		bottom_panel_button->show();
		make_bottom_panel_item_visible(graph_editor);
	} else {
		if (graph_editor->is_visible_in_tree()) {
			hide_bottom_panel();
		}
		bottom_panel_button->hide();
	}
}
