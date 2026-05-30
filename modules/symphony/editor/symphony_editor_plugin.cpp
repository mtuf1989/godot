#include "symphony_editor_plugin.h"

#include "core/io/resource_saver.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"
#include "scene/gui/label.h"

// --- SymphonyNodeInspectorProxy ---

void SymphonyNodeInspectorProxy::_bind_methods() {
}

void SymphonyNodeInspectorProxy::setup(SymphonyGraphEditor *p_editor, int32_t p_node_id, const StringName &p_type_name) {
	editor = p_editor;
	node_id = p_node_id;
	type_name = p_type_name;
	notify_property_list_changed();
}

void SymphonyNodeInspectorProxy::_get_property_list(List<PropertyInfo> *p_list) const {
	p_list->push_back(PropertyInfo(Variant::STRING, "node_type", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY));

	const OperatorRegistry *reg = OperatorRegistry::get_singleton();
	const OperatorDescriptor *op_desc = reg ? reg->find(type_name) : nullptr;
	if (!op_desc) {
		return;
	}

	for (int i = 0; i < op_desc->params.size(); i++) {
		const ParamDescriptor &pd = op_desc->params[i];
		p_list->push_back(PropertyInfo(Variant::FLOAT, pd.name, PROPERTY_HINT_RANGE, vformat("%f,%f,%f", pd.min_value, pd.max_value, pd.step)));
	}
}

bool SymphonyNodeInspectorProxy::_get(const StringName &p_name, Variant &r_ret) const {
	if (p_name == StringName("node_type")) {
		r_ret = String(type_name);
		return true;
	}

	if (!editor || !editor->get_edited_stream().is_valid()) {
		return false;
	}

	const GraphDescription &desc = editor->get_edited_stream()->get_graph_description();
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == node_id) {
			if (desc.nodes[i].params.has(p_name)) {
				r_ret = desc.nodes[i].params[p_name];
				return true;
			}
			break;
		}
	}
	return false;
}

bool SymphonyNodeInspectorProxy::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name == StringName("node_type")) {
		return true;
	}

	if (!editor || !editor->get_edited_stream().is_valid()) {
		return false;
	}

	// Directly update the param (undo/redo is handled by the inline SpinBox).
	const GraphDescription &desc = editor->get_edited_stream()->get_graph_description();
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == node_id) {
			if (desc.nodes[i].params.has(p_name)) {
				editor->_ur_set_param(node_id, p_name, (float)p_value);
				return true;
			}
			break;
		}
	}
	return false;
}

// --- Static clipboard ---
SymphonyGraphEditor::ClipboardData SymphonyGraphEditor::clipboard;

// --- SymphonyGraphEditor ---

void SymphonyGraphEditor::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_ur_add_node", "node_data"), &SymphonyGraphEditor::_ur_add_node);
	ClassDB::bind_method(D_METHOD("_ur_remove_node", "node_id"), &SymphonyGraphEditor::_ur_remove_node);
	ClassDB::bind_method(D_METHOD("_ur_connect", "from", "from_pin", "to", "to_pin"), &SymphonyGraphEditor::_ur_connect);
	ClassDB::bind_method(D_METHOD("_ur_disconnect", "from", "from_pin", "to", "to_pin"), &SymphonyGraphEditor::_ur_disconnect);
	ClassDB::bind_method(D_METHOD("_ur_set_param", "node_id", "param_name", "value"), &SymphonyGraphEditor::_ur_set_param);
	ClassDB::bind_method(D_METHOD("_ur_set_node_position", "node_id", "position"), &SymphonyGraphEditor::_ur_set_node_position);
	ClassDB::bind_method(D_METHOD("_ur_add_frame", "frame_data"), &SymphonyGraphEditor::_ur_add_frame);
	ClassDB::bind_method(D_METHOD("_ur_remove_frame", "frame_id"), &SymphonyGraphEditor::_ur_remove_frame);
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

	add_frame_button = memnew(Button);
	add_frame_button->set_text("Add Comment");
	add_frame_button->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_on_add_frame_pressed));
	toolbar->add_child(add_frame_button);

	preview_button = memnew(Button);
	preview_button->set_text("Preview");
	preview_button->set_toggle_mode(true);
	preview_button->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_on_preview_pressed));
	toolbar->add_child(preview_button);

	save_button = memnew(Button);
	save_button->set_text("Save");
	save_button->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_on_save_pressed));
	toolbar->add_child(save_button);

	Button *delete_button = memnew(Button);
	delete_button->set_text("Delete");
	delete_button->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_on_delete_pressed));
	toolbar->add_child(delete_button);

	// Breadcrumb bar (for SubGraph navigation).
	breadcrumb_bar = memnew(HBoxContainer);
	breadcrumb_bar->hide(); // Hidden until we navigate into a sub-graph.
	add_child(breadcrumb_bar);

	// GraphEdit
	graph_edit = memnew(GraphEdit);
	graph_edit->set_v_size_flags(SIZE_EXPAND_FILL);
	graph_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	graph_edit->set_custom_minimum_size(Vector2(0, 300));
	graph_edit->set_connection_lines_thickness(2.5f);
	add_child(graph_edit);

	// Register valid connection types (same type + Float->Audio promotion).
	for (int i = 0; i <= SLOT_TRIGGER; i++) {
		graph_edit->add_valid_connection_type(i, i);
	}
	graph_edit->add_valid_connection_type(SLOT_FLOAT, SLOT_AUDIO);

	graph_edit->connect("connection_request", callable_mp(this, &SymphonyGraphEditor::_on_connection_request));
	graph_edit->connect("disconnection_request", callable_mp(this, &SymphonyGraphEditor::_on_disconnection_request));
	graph_edit->connect("delete_nodes_request", callable_mp(this, &SymphonyGraphEditor::_on_delete_nodes_request));
	graph_edit->connect("copy_nodes_request", callable_mp(this, &SymphonyGraphEditor::_on_copy_nodes_request));
	graph_edit->connect("paste_nodes_request", callable_mp(this, &SymphonyGraphEditor::_on_paste_nodes_request));
	graph_edit->connect("duplicate_nodes_request", callable_mp(this, &SymphonyGraphEditor::_on_duplicate_nodes_request));
	graph_edit->connect("node_selected", callable_mp(this, &SymphonyGraphEditor::_on_node_selected));

	// Inspector proxy.
	inspector_proxy = memnew(SymphonyNodeInspectorProxy);

	// Preview player (hidden)
	preview_player = memnew(AudioStreamPlayer);
	add_child(preview_player);
}

SymphonyGraphEditor::~SymphonyGraphEditor() {
	if (inspector_proxy) {
		memdelete(inspector_proxy);
	}
}

Color SymphonyGraphEditor::get_pin_color(SymphonyPinType p_type) const {
	switch (p_type) {
		case SymphonyPinType::AUDIO:
			return Color(0.4, 0.6, 1.0);
		case SymphonyPinType::FLOAT:
			return Color(0.4, 0.9, 0.4);
		case SymphonyPinType::INT:
			return Color(0.9, 0.9, 0.3);
		case SymphonyPinType::BOOL:
			return Color(0.9, 0.9, 0.9);
		case SymphonyPinType::TRIGGER:
			return Color(1.0, 0.6, 0.2);
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

	HashMap<String, Vector<int>> categories;
	for (int i = 0; i < types.size(); i++) {
		const OperatorDescriptor *desc = reg->find(types[i]);
		if (desc->type_name == StringName("SubGraph")) {
			continue; // SubGraph gets special menu entries below.
		}
		String cat = desc && !desc->category.is_empty() ? desc->category : "Other";
		if (!categories.has(cat)) {
			categories[cat] = Vector<int>();
		}
		categories[cat].push_back(i);
	}

	for (const KeyValue<String, Vector<int>> &kv : categories) {
		PopupMenu *sub = memnew(PopupMenu);
		String safe_name = kv.key.replace("/", "_").replace(" ", "_");
		sub->set_name(safe_name);
		for (int idx : kv.value) {
			sub->add_item(String(types[idx]), idx);
		}
		sub->connect("id_pressed", callable_mp(this, &SymphonyGraphEditor::_on_add_node_id_pressed));
		popup->add_child(sub);
		popup->add_submenu_item(kv.key, safe_name);
	}

	// SubGraph special entries.
	popup->add_separator();
	popup->add_item("Add SubGraph (from file)...", SUBGRAPH_MENU_ID);
	popup->add_item("Create New SubGraph...", CREATE_SUBGRAPH_MENU_ID);
}

void SymphonyGraphEditor::edit(Ref<AudioStreamSymphony> p_stream) {
	if (stream == p_stream) {
		return;
	}
	stream = p_stream;
	nav_stack.clear();

	next_node_id = 0;
	next_frame_id = 0;
	if (stream.is_valid()) {
		const GraphDescription &desc = stream->get_graph_description();
		for (int i = 0; i < desc.nodes.size(); i++) {
			if (desc.nodes[i].id >= next_node_id) {
				next_node_id = desc.nodes[i].id + 1;
			}
		}
		for (int i = 0; i < desc.frames.size(); i++) {
			if (desc.frames[i].id >= next_frame_id) {
				next_frame_id = desc.frames[i].id + 1;
			}
		}
	}

	_rebuild_graph_edit();
	_rebuild_breadcrumbs();
}

void SymphonyGraphEditor::_rebuild_graph_edit() {
	graph_edit->clear_connections();
	Vector<Node *> to_remove;
	for (int i = 0; i < graph_edit->get_child_count(); i++) {
		if (Object::cast_to<GraphNode>(graph_edit->get_child(i)) || Object::cast_to<GraphFrame>(graph_edit->get_child(i))) {
			to_remove.push_back(graph_edit->get_child(i));
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

	for (int i = 0; i < desc.nodes.size(); i++) {
		GraphNode *gn = _create_graph_node(desc.nodes[i]);
		graph_edit->add_child(gn);
	}

	for (int i = 0; i < desc.connections.size(); i++) {
		const ConnectionDesc &conn = desc.connections[i];
		graph_edit->connect_node(_name_from_node_id(conn.from_node), conn.from_pin, _name_from_node_id(conn.to_node), conn.to_pin);
	}

	// Create frames.
	for (int i = 0; i < desc.frames.size(); i++) {
		const FrameDesc &fd = desc.frames[i];
		_create_frame_ui(fd);

		for (int32_t nid : fd.attached_nodes) {
			StringName node_name = _name_from_node_id(nid);
			if (graph_edit->get_node_or_null(NodePath(String(node_name)))) {
				graph_edit->attach_graph_element_to_frame(node_name, _frame_name_from_id(fd.id));
			}
		}
	}
}

GraphNode *SymphonyGraphEditor::_create_graph_node(const NodeDesc &p_node) {
	// SubGraph nodes get special rendering with dynamic pins.
	if (p_node.type_name == StringName("SubGraph")) {
		return _create_subgraph_node(p_node);
	}

	GraphNode *gn = memnew(GraphNode);
	gn->set_name(_name_from_node_id(p_node.id));
	gn->set_title(String(p_node.type_name));
	gn->set_position_offset(p_node.editor_position);

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
		bool has_left = i < op_desc->inputs.size();
		bool has_right = i < op_desc->outputs.size();

		HBoxContainer *row = memnew(HBoxContainer);
		if (has_left) {
			Label *left_lbl = memnew(Label);
			left_lbl->set_text(String(op_desc->inputs[i].name));
			left_lbl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			row->add_child(left_lbl);
		} else {
			Control *spacer = memnew(Control);
			spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
			row->add_child(spacer);
		}
		if (has_right) {
			Label *right_lbl = memnew(Label);
			right_lbl->set_text(String(op_desc->outputs[i].name));
			right_lbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
			row->add_child(right_lbl);
		}
		gn->add_child(row);

		int left_type = has_left ? get_slot_type(op_desc->inputs[i].type) : 0;
		Color left_color = has_left ? get_pin_color(op_desc->inputs[i].type) : Color(1, 1, 1);
		int right_type = has_right ? get_slot_type(op_desc->outputs[i].type) : 0;
		Color right_color = has_right ? get_pin_color(op_desc->outputs[i].type) : Color(1, 1, 1);

		gn->set_slot(i, has_left, left_type, left_color, has_right, right_type, right_color);
	}

	// Parameter editors — special handling for GraphInput/GraphOutput.
	bool is_io_node = (p_node.type_name == StringName("GraphInput") || p_node.type_name == StringName("GraphInputAudio") || p_node.type_name == StringName("GraphOutput"));
	int slot_idx = max_slots;

	for (int i = 0; i < op_desc->params.size(); i++) {
		const ParamDescriptor &pd = op_desc->params[i];

		// Skip string-type params that are stored as Variant strings (parameter_name, display_name).
		if (pd.name == StringName("parameter_name") || pd.name == StringName("display_name")) {
			continue;
		}

		// resource_path: show a file picker button (except for SubGraph which has its own UI).
		if (pd.name == StringName("resource_path") && p_node.type_name != StringName("SubGraph")) {
			HBoxContainer *row = memnew(HBoxContainer);
			row->set_meta("_param_row", true);

			Button *pick_btn = memnew(Button);
			String current_path;
			if (p_node.params.has("resource_path")) {
				current_path = String(p_node.params["resource_path"]);
			}
			pick_btn->set_text(current_path.is_empty() ? "[Select WAV...]" : current_path.get_file());
			pick_btn->set_custom_minimum_size(Vector2(120, 0));
			pick_btn->set_clip_text(true);
			pick_btn->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_on_resource_pick_pressed).bind(p_node.id));
			row->add_child(pick_btn);
			gn->add_child(row);
			gn->set_slot(slot_idx, false, 0, Color(), false, 0, Color());
			slot_idx++;
			continue;
		}

		// pin_type gets a dropdown instead of SpinBox.
		if (pd.name == StringName("pin_type") && is_io_node) {
			HBoxContainer *row = memnew(HBoxContainer);
			row->set_meta("_param_row", true);
			Label *lbl = memnew(Label);
			lbl->set_text("pin_type:");
			lbl->set_custom_minimum_size(Vector2(60, 0));
			row->add_child(lbl);

			OptionButton *opt = memnew(OptionButton);
			opt->add_item("Audio", 0);
			opt->add_item("Float", 1);
			opt->add_item("Int", 2);
			opt->add_item("Bool", 3);
			opt->add_item("Trigger", 4);
			opt->set_custom_minimum_size(Vector2(80, 0));

			int current = 0;
			if (p_node.params.has("pin_type")) {
				current = (int)(float)p_node.params["pin_type"];
			} else {
				// Default: GraphInput=FLOAT(1), GraphOutput=AUDIO(0)
				current = (p_node.type_name == StringName("GraphInput")) ? 1 : 0;
			}
			opt->select(current);
			opt->connect("item_selected", callable_mp(this, &SymphonyGraphEditor::_on_pin_type_changed).bind(p_node.id));
			row->add_child(opt);
			gn->add_child(row);
			gn->set_slot(slot_idx, false, 0, Color(), false, 0, Color());
			slot_idx++;
			continue;
		}

		HBoxContainer *row = memnew(HBoxContainer);
		row->set_meta("_param_row", true);
		Label *lbl = memnew(Label);
		lbl->set_text(String(pd.name) + ":");
		lbl->set_custom_minimum_size(Vector2(60, 0));
		row->add_child(lbl);

		SpinBox *spin = memnew(SpinBox);
		spin->set_min(pd.min_value);
		spin->set_max(pd.max_value);
		spin->set_step(pd.step);
		spin->set_custom_minimum_size(Vector2(80, 0));

		float val = pd.default_value;
		if (p_node.params.has(pd.name)) {
			val = p_node.params[pd.name];
		}
		spin->set_value(val);
		spin->connect("value_changed", callable_mp(this, &SymphonyGraphEditor::_on_param_changed).bind(p_node.id, pd.name));
		row->add_child(spin);
		gn->add_child(row);

		gn->set_slot(slot_idx, false, 0, Color(), false, 0, Color());
		slot_idx++;
	}

	// Collapse button.
	if (op_desc->params.size() > 0) {
		HBoxContainer *titlebar = gn->get_titlebar_hbox();
		Button *collapse_btn = memnew(Button);
		collapse_btn->set_text(p_node.collapsed ? ">" : "v");
		collapse_btn->set_toggle_mode(true);
		collapse_btn->set_pressed(p_node.collapsed);
		collapse_btn->set_custom_minimum_size(Vector2(24, 24));
		collapse_btn->set_flat(true);
		titlebar->add_child(collapse_btn);
		collapse_btn->connect("toggled", callable_mp(this, &SymphonyGraphEditor::_on_collapse_toggled).bind(p_node.id));

		if (p_node.collapsed) {
			_set_params_visible(gn, false);
			gn->reset_size();
		}
	}

	gn->connect("position_offset_changed", callable_mp(this, &SymphonyGraphEditor::_on_node_position_changed).bind(gn->get_name()));

	return gn;
}

// --- Event handlers (with undo/redo) ---

void SymphonyGraphEditor::_on_add_node_id_pressed(int p_id) {
	if (!stream.is_valid()) {
		return;
	}

	// Handle SubGraph special menu entries.
	if (p_id == SUBGRAPH_MENU_ID) {
		pending_subgraph_node_id = next_node_id++;
		pending_resource_node_id = -1;
		if (!file_dialog) {
			file_dialog = memnew(FileDialog);
			file_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
			file_dialog->set_access(FileDialog::ACCESS_RESOURCES);
			file_dialog->add_filter("*.tres", "Symphony Resource");
			file_dialog->connect("file_selected", callable_mp(this, &SymphonyGraphEditor::_on_file_dialog_file_selected));
			add_child(file_dialog);
		}
		file_dialog->set_title("Select SubGraph Resource");
		file_dialog->popup_centered_ratio(0.6);
		return;
	}

	if (p_id == CREATE_SUBGRAPH_MENU_ID) {
		_on_create_new_subgraph();
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

	const OperatorDescriptor *op_desc = reg->find(nd.type_name);
	if (op_desc) {
		for (int i = 0; i < op_desc->params.size(); i++) {
			nd.params[op_desc->params[i].name] = op_desc->params[i].default_value;
		}
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Add Symphony Node");
	undo_redo->add_do_method(this, "_ur_add_node", _node_desc_to_dict(nd));
	undo_redo->add_undo_method(this, "_ur_remove_node", nd.id);
	undo_redo->commit_action();
}

void SymphonyGraphEditor::_on_connection_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port) {
	if (!stream.is_valid()) {
		return;
	}

	int32_t from_id = _node_id_from_name(p_from);
	int32_t to_id = _node_id_from_name(p_to);

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Connect Symphony Nodes");
	undo_redo->add_do_method(this, "_ur_connect", from_id, p_from_port, to_id, p_to_port);
	undo_redo->add_undo_method(this, "_ur_disconnect", from_id, p_from_port, to_id, p_to_port);
	undo_redo->commit_action();
}

void SymphonyGraphEditor::_on_disconnection_request(const StringName &p_from, int p_from_port, const StringName &p_to, int p_to_port) {
	if (!stream.is_valid()) {
		return;
	}

	int32_t from_id = _node_id_from_name(p_from);
	int32_t to_id = _node_id_from_name(p_to);

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Disconnect Symphony Nodes");
	undo_redo->add_do_method(this, "_ur_disconnect", from_id, p_from_port, to_id, p_to_port);
	undo_redo->add_undo_method(this, "_ur_connect", from_id, p_from_port, to_id, p_to_port);
	undo_redo->commit_action();
}

void SymphonyGraphEditor::_on_delete_nodes_request(const TypedArray<StringName> &p_nodes) {
	if (!stream.is_valid()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Delete Symphony Nodes");

	const GraphDescription &desc = stream->get_graph_description();
	bool has_actions = false;

	for (int i = 0; i < p_nodes.size(); i++) {
		int32_t node_id = _node_id_from_name(p_nodes[i]);
		if (node_id < 0) {
			continue; // Skip frames or invalid names.
		}

		// Find and record connections to undo.
		for (int c = 0; c < desc.connections.size(); c++) {
			const ConnectionDesc &conn = desc.connections[c];
			if (conn.from_node == node_id || conn.to_node == node_id) {
				undo_redo->add_do_method(this, "_ur_disconnect", conn.from_node, conn.from_pin, conn.to_node, conn.to_pin);
				undo_redo->add_undo_method(this, "_ur_connect", conn.from_node, conn.from_pin, conn.to_node, conn.to_pin);
			}
		}

		// Find the node data for undo.
		for (int n = 0; n < desc.nodes.size(); n++) {
			if (desc.nodes[n].id == node_id) {
				undo_redo->add_do_method(this, "_ur_remove_node", node_id);
				undo_redo->add_undo_method(this, "_ur_add_node", _node_desc_to_dict(desc.nodes[n]));
				has_actions = true;
				break;
			}
		}
	}

	if (has_actions) {
		undo_redo->commit_action();
	}
}

void SymphonyGraphEditor::_on_delete_pressed() {
	if (!stream.is_valid()) {
		return;
	}

	// Collect selected nodes.
	TypedArray<StringName> selected;
	for (int i = 0; i < graph_edit->get_child_count(); i++) {
		GraphNode *gn = Object::cast_to<GraphNode>(graph_edit->get_child(i));
		if (gn && gn->is_selected()) {
			selected.push_back(gn->get_name());
		}
	}

	if (!selected.is_empty()) {
		_on_delete_nodes_request(selected);
	}
}

void SymphonyGraphEditor::_on_node_position_changed(const StringName &p_node) {
	if (!stream.is_valid()) {
		return;
	}

	int32_t node_id = _node_id_from_name(p_node);
	Node *child = graph_edit->get_node_or_null(NodePath(String(p_node)));
	GraphNode *gn = Object::cast_to<GraphNode>(child);
	if (!gn) {
		return;
	}

	Vector2 new_pos = gn->get_position_offset();

	// Find old position.
	const GraphDescription &desc = stream->get_graph_description();
	Vector2 old_pos = new_pos;
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == node_id) {
			old_pos = desc.nodes[i].editor_position;
			break;
		}
	}

	if (old_pos == new_pos) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Move Symphony Node", UndoRedo::MERGE_ENDS);
	undo_redo->add_do_method(this, "_ur_set_node_position", node_id, new_pos);
	undo_redo->add_undo_method(this, "_ur_set_node_position", node_id, old_pos);
	undo_redo->commit_action(false); // Don't execute do — already moved visually.
}

void SymphonyGraphEditor::_on_param_changed(double p_value, int32_t p_node_id, const StringName &p_param_name) {
	if (!stream.is_valid()) {
		return;
	}

	// Find old value (description hasn't been updated yet, SpinBox fires before we update).
	float old_value = p_value;
	const GraphDescription &desc = stream->get_graph_description();
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == p_node_id && desc.nodes[i].params.has(p_param_name)) {
			old_value = desc.nodes[i].params[p_param_name];
			break;
		}
	}

	if (old_value == (float)p_value) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Change Symphony Param", UndoRedo::MERGE_ENDS);
	undo_redo->add_do_method(this, "_ur_set_param", p_node_id, p_param_name, (float)p_value);
	undo_redo->add_undo_method(this, "_ur_set_param", p_node_id, p_param_name, old_value);
	undo_redo->commit_action();
}

void SymphonyGraphEditor::_on_collapse_toggled(bool p_collapsed, int32_t p_node_id) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == p_node_id) {
			desc.nodes.write[i].collapsed = p_collapsed;
			break;
		}
	}

	StringName node_name = _name_from_node_id(p_node_id);
	Node *child = graph_edit->get_node_or_null(NodePath(String(node_name)));
	GraphNode *gn = Object::cast_to<GraphNode>(child);
	if (gn) {
		_set_params_visible(gn, !p_collapsed);
		gn->reset_size();
	}

	stream->emit_changed();
}

void SymphonyGraphEditor::_set_params_visible(GraphNode *p_node, bool p_visible) {
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *child = p_node->get_child(i);
		if (child->has_meta("_param_row")) {
			Object::cast_to<Control>(child)->set_visible(p_visible);
		}
	}
}

void SymphonyGraphEditor::_on_node_selected(Node *p_node) {
	GraphNode *gn = Object::cast_to<GraphNode>(p_node);
	if (gn && stream.is_valid()) {
		int32_t node_id = _node_id_from_name(gn->get_name());
		const GraphDescription &desc = stream->get_graph_description();
		for (int i = 0; i < desc.nodes.size(); i++) {
			if (desc.nodes[i].id == node_id) {
				inspector_proxy->setup(this, node_id, desc.nodes[i].type_name);
				EditorNode::get_singleton()->push_item(inspector_proxy, "", true);
				return;
			}
		}
	}

	GraphFrame *frame = Object::cast_to<GraphFrame>(p_node);
	if (frame) {
		// Push the frame itself so user can edit title/color in Inspector.
		EditorNode::get_singleton()->push_item(frame, "", true);
	}
}

// --- Undo/Redo helpers ---

void SymphonyGraphEditor::_ur_add_node(const Dictionary &p_node_data) {
	if (!stream.is_valid()) {
		return;
	}

	NodeDesc nd = _dict_to_node_desc(p_node_data);
	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	desc.nodes.push_back(nd);

	GraphNode *gn = _create_graph_node(nd);
	graph_edit->add_child(gn);

	if (nd.id >= next_node_id) {
		next_node_id = nd.id + 1;
	}

	stream->notify_property_list_changed();
	stream->emit_changed();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_ur_remove_node(int32_t p_node_id) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());

	// Remove connections involving this node from GraphEdit.
	for (int c = desc.connections.size() - 1; c >= 0; c--) {
		if (desc.connections[c].from_node == p_node_id || desc.connections[c].to_node == p_node_id) {
			graph_edit->disconnect_node(
					_name_from_node_id(desc.connections[c].from_node), desc.connections[c].from_pin,
					_name_from_node_id(desc.connections[c].to_node), desc.connections[c].to_pin);
			desc.connections.remove_at(c);
		}
	}

	// Remove node from desc.
	for (int n = 0; n < desc.nodes.size(); n++) {
		if (desc.nodes[n].id == p_node_id) {
			desc.nodes.remove_at(n);
			break;
		}
	}

	// Remove from GraphEdit.
	StringName node_name = _name_from_node_id(p_node_id);
	Node *child = graph_edit->get_node_or_null(NodePath(String(node_name)));
	if (child) {
		graph_edit->remove_child(child);
		child->queue_free();
	}

	stream->notify_property_list_changed();
	stream->emit_changed();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_ur_connect(int32_t p_from, int p_from_pin, int32_t p_to, int p_to_pin) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	ConnectionDesc cd;
	cd.from_node = p_from;
	cd.from_pin = p_from_pin;
	cd.to_node = p_to;
	cd.to_pin = p_to_pin;
	desc.connections.push_back(cd);

	graph_edit->connect_node(_name_from_node_id(p_from), p_from_pin, _name_from_node_id(p_to), p_to_pin);

	stream->notify_property_list_changed();
	stream->emit_changed();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_ur_disconnect(int32_t p_from, int p_from_pin, int32_t p_to, int p_to_pin) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	for (int i = desc.connections.size() - 1; i >= 0; i--) {
		const ConnectionDesc &c = desc.connections[i];
		if (c.from_node == p_from && c.from_pin == p_from_pin && c.to_node == p_to && c.to_pin == p_to_pin) {
			desc.connections.remove_at(i);
			break;
		}
	}

	graph_edit->disconnect_node(_name_from_node_id(p_from), p_from_pin, _name_from_node_id(p_to), p_to_pin);

	stream->notify_property_list_changed();
	stream->emit_changed();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_ur_set_param(int32_t p_node_id, const StringName &p_param_name, float p_value) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == p_node_id) {
			desc.nodes.write[i].params[p_param_name] = p_value;
			break;
		}
	}

	// Update SpinBox UI — find the param row by iterating children with _param_row meta.
	StringName node_name = _name_from_node_id(p_node_id);
	Node *child = graph_edit->get_node_or_null(NodePath(String(node_name)));
	GraphNode *gn = Object::cast_to<GraphNode>(child);
	if (gn) {
		const OperatorRegistry *reg = OperatorRegistry::get_singleton();
		const OperatorDescriptor *op_desc = nullptr;
		for (int i = 0; i < desc.nodes.size(); i++) {
			if (desc.nodes[i].id == p_node_id) {
				op_desc = reg ? reg->find(desc.nodes[i].type_name) : nullptr;
				break;
			}
		}
		if (op_desc) {
			// Find which param index this is.
			int param_idx = -1;
			for (int i = 0; i < op_desc->params.size(); i++) {
				if (op_desc->params[i].name == p_param_name) {
					param_idx = i;
					break;
				}
			}
			if (param_idx >= 0) {
				// Find the param_idx-th child with _param_row meta.
				int found = 0;
				for (int i = 0; i < gn->get_child_count(); i++) {
					Node *c = gn->get_child(i);
					if (c->has_meta("_param_row")) {
						if (found == param_idx) {
							HBoxContainer *row = Object::cast_to<HBoxContainer>(c);
							if (row && row->get_child_count() >= 2) {
								SpinBox *spin = Object::cast_to<SpinBox>(row->get_child(1));
								if (spin) {
									spin->set_value_no_signal(p_value);
								}
							}
							break;
						}
						found++;
					}
				}
			}
		}
	}

	stream->emit_changed();
	_recompile_and_preview();
}

void SymphonyGraphEditor::_ur_set_node_position(int32_t p_node_id, const Vector2 &p_position) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == p_node_id) {
			desc.nodes.write[i].editor_position = p_position;
			break;
		}
	}

	StringName node_name = _name_from_node_id(p_node_id);
	Node *child = graph_edit->get_node_or_null(NodePath(String(node_name)));
	GraphNode *gn = Object::cast_to<GraphNode>(child);
	if (gn) {
		gn->set_position_offset(p_position);
	}

	stream->emit_changed();
}

void SymphonyGraphEditor::_ur_add_frame(const Dictionary &p_frame_data) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	FrameDesc fd;
	fd.id = p_frame_data["id"];
	fd.title = p_frame_data["title"];
	fd.editor_position = p_frame_data["position"];
	fd.editor_size = p_frame_data["size"];
	fd.tint_color = p_frame_data["tint_color"];
	desc.frames.push_back(fd);

	_create_frame_ui(fd);

	if (fd.id >= next_frame_id) {
		next_frame_id = fd.id + 1;
	}

	stream->notify_property_list_changed();
	stream->emit_changed();
}

void SymphonyGraphEditor::_ur_remove_frame(int32_t p_frame_id) {
	if (!stream.is_valid()) {
		return;
	}

	GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
	for (int i = 0; i < desc.frames.size(); i++) {
		if (desc.frames[i].id == p_frame_id) {
			desc.frames.remove_at(i);
			break;
		}
	}

	StringName frame_name = _frame_name_from_id(p_frame_id);
	Node *child = graph_edit->get_node_or_null(NodePath(String(frame_name)));
	if (child) {
		graph_edit->remove_child(child);
		child->queue_free();
	}

	stream->notify_property_list_changed();
	stream->emit_changed();
}

// --- Comment Frames ---

void SymphonyGraphEditor::_on_add_frame_pressed() {
	if (!stream.is_valid()) {
		return;
	}

	Dictionary fd;
	fd["id"] = next_frame_id++;
	fd["title"] = "Comment";
	fd["position"] = graph_edit->get_scroll_offset() + Vector2(50, 50);
	fd["size"] = Vector2(300, 200);
	fd["tint_color"] = Color(0.3, 0.3, 0.3, 0.75);

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Add Comment Frame");
	undo_redo->add_do_method(this, "_ur_add_frame", fd);
	undo_redo->add_undo_method(this, "_ur_remove_frame", fd["id"]);
	undo_redo->commit_action();
}

void SymphonyGraphEditor::_create_frame_ui(const FrameDesc &p_frame) {
	GraphFrame *frame = memnew(GraphFrame);
	frame->set_name(_frame_name_from_id(p_frame.id));
	frame->set_title(p_frame.title);
	frame->set_position_offset(p_frame.editor_position);
	frame->set_size(p_frame.editor_size);
	frame->set_resizable(true);
	frame->set_autoshrink_enabled(false);
	frame->set_tint_color_enabled(true);
	frame->set_tint_color(p_frame.tint_color);
	graph_edit->add_child(frame);
}

// --- Copy/Paste/Duplicate ---

void SymphonyGraphEditor::_on_copy_nodes_request() {
	if (!stream.is_valid()) {
		return;
	}

	clipboard.nodes.clear();
	clipboard.connections.clear();

	const GraphDescription &desc = stream->get_graph_description();

	// Collect selected node IDs.
	HashSet<int32_t> selected_ids;
	for (int i = 0; i < graph_edit->get_child_count(); i++) {
		GraphNode *gn = Object::cast_to<GraphNode>(graph_edit->get_child(i));
		if (gn && gn->is_selected()) {
			selected_ids.insert(_node_id_from_name(gn->get_name()));
		}
	}

	if (selected_ids.is_empty()) {
		return;
	}

	// Copy nodes and compute center.
	Vector2 center;
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (selected_ids.has(desc.nodes[i].id)) {
			clipboard.nodes.push_back(desc.nodes[i]);
			center += desc.nodes[i].editor_position;
		}
	}
	clipboard.center = center / clipboard.nodes.size();

	// Copy internal connections (both endpoints selected).
	for (int i = 0; i < desc.connections.size(); i++) {
		const ConnectionDesc &c = desc.connections[i];
		if (selected_ids.has(c.from_node) && selected_ids.has(c.to_node)) {
			clipboard.connections.push_back(c);
		}
	}
}

void SymphonyGraphEditor::_on_paste_nodes_request() {
	if (!stream.is_valid() || clipboard.nodes.is_empty()) {
		return;
	}

	Vector2 paste_offset = graph_edit->get_scroll_offset() + Vector2(150, 150) - clipboard.center;

	// Remap IDs.
	HashMap<int32_t, int32_t> id_map;
	for (int i = 0; i < clipboard.nodes.size(); i++) {
		id_map[clipboard.nodes[i].id] = next_node_id + i;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Paste Symphony Nodes");

	for (int i = 0; i < clipboard.nodes.size(); i++) {
		NodeDesc nd = clipboard.nodes[i];
		nd.id = id_map[clipboard.nodes[i].id];
		nd.editor_position += paste_offset;
		undo_redo->add_do_method(this, "_ur_add_node", _node_desc_to_dict(nd));
		undo_redo->add_undo_method(this, "_ur_remove_node", nd.id);
	}

	for (int i = 0; i < clipboard.connections.size(); i++) {
		const ConnectionDesc &c = clipboard.connections[i];
		int32_t new_from = id_map[c.from_node];
		int32_t new_to = id_map[c.to_node];
		undo_redo->add_do_method(this, "_ur_connect", new_from, c.from_pin, new_to, c.to_pin);
		undo_redo->add_undo_method(this, "_ur_disconnect", new_from, c.from_pin, new_to, c.to_pin);
	}

	next_node_id += clipboard.nodes.size();
	undo_redo->commit_action();
}

void SymphonyGraphEditor::_on_duplicate_nodes_request() {
	_on_copy_nodes_request();
	_on_paste_nodes_request();
}

// --- SubGraph support ---

GraphNode *SymphonyGraphEditor::_create_subgraph_node(const NodeDesc &p_node) {
	GraphNode *gn = memnew(GraphNode);
	gn->set_name(_name_from_node_id(p_node.id));
	gn->set_position_offset(p_node.editor_position);

	String resource_path;
	if (p_node.params.has("resource_path")) {
		resource_path = String(p_node.params["resource_path"]);
	}

	// Title shows the filename.
	String title = "SubGraph";
	if (!resource_path.is_empty()) {
		title = resource_path.get_file().get_basename();
	}
	gn->set_title(title);

	// Load the referenced resource to determine pins.
	if (!resource_path.is_empty()) {
		Ref<AudioStreamSymphony> sub_res = ResourceLoader::load(resource_path, "AudioStreamSymphony");
		if (sub_res.is_valid()) {
			const GraphDescription &sub_desc = sub_res->get_graph_description();

			// Collect input pins (GraphInput nodes) and output pins (GraphOutput nodes).
			struct PinInfo {
				String name;
				int sort_order = 0;
				float editor_y = 0.0f;
				SymphonyPinType type = SymphonyPinType::FLOAT;
			};
			struct PinInfoCompare {
				bool operator()(const PinInfo &a, const PinInfo &b) const {
					if (a.sort_order != b.sort_order) return a.sort_order < b.sort_order;
					if (a.editor_y != b.editor_y) return a.editor_y < b.editor_y;
					return a.name < b.name;
				}
			};

			Vector<PinInfo> inputs;
			Vector<PinInfo> outputs;

			for (const NodeDesc &nd : sub_desc.nodes) {
				if (nd.type_name == StringName("GraphInput")) {
					PinInfo pi;
					pi.name = nd.params.has("parameter_name") ? String(nd.params["parameter_name"]) : "in";
					pi.sort_order = nd.params.has("sort_order") ? (int)(float)nd.params["sort_order"] : 0;
					pi.editor_y = nd.editor_position.y;
					int pt = nd.params.has("pin_type") ? (int)(float)nd.params["pin_type"] : 1; // default FLOAT
					pi.type = (SymphonyPinType)pt;
					inputs.push_back(pi);
				} else if (nd.type_name == StringName("GraphInputAudio")) {
					PinInfo pi;
					pi.name = nd.params.has("parameter_name") ? String(nd.params["parameter_name"]) : "audio_in";
					pi.sort_order = nd.params.has("sort_order") ? (int)(float)nd.params["sort_order"] : 0;
					pi.editor_y = nd.editor_position.y;
					pi.type = SymphonyPinType::AUDIO;
					inputs.push_back(pi);
				} else if (nd.type_name == StringName("GraphOutput")) {
					PinInfo pi;
					pi.name = nd.params.has("display_name") ? String(nd.params["display_name"]) : "out";
					if (pi.name.is_empty()) {
						pi.name = "out";
					}
					pi.sort_order = nd.params.has("sort_order") ? (int)(float)nd.params["sort_order"] : 0;
					pi.editor_y = nd.editor_position.y;
					int pt = nd.params.has("pin_type") ? (int)(float)nd.params["pin_type"] : 0; // default AUDIO
					pi.type = (SymphonyPinType)pt;
					outputs.push_back(pi);
				}
			}

			inputs.sort_custom<PinInfoCompare>();
			outputs.sort_custom<PinInfoCompare>();

			int max_slots = MAX(inputs.size(), outputs.size());
			for (int i = 0; i < max_slots; i++) {
				bool has_left = i < inputs.size();
				bool has_right = i < outputs.size();

				HBoxContainer *row = memnew(HBoxContainer);
				if (has_left) {
					Label *left_lbl = memnew(Label);
					left_lbl->set_text(inputs[i].name);
					left_lbl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
					row->add_child(left_lbl);
				} else {
					Control *spacer = memnew(Control);
					spacer->set_h_size_flags(Control::SIZE_EXPAND_FILL);
					row->add_child(spacer);
				}
				if (has_right) {
					Label *right_lbl = memnew(Label);
					right_lbl->set_text(outputs[i].name);
					right_lbl->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_RIGHT);
					row->add_child(right_lbl);
				}
				gn->add_child(row);

				int left_type = has_left ? get_slot_type(inputs[i].type) : 0;
				Color left_color = has_left ? get_pin_color(inputs[i].type) : Color(1, 1, 1);
				int right_type = has_right ? get_slot_type(outputs[i].type) : 0;
				Color right_color = has_right ? get_pin_color(outputs[i].type) : Color(1, 1, 1);

				gn->set_slot(i, has_left, left_type, left_color, has_right, right_type, right_color);
			}
		} else {
			Label *lbl = memnew(Label);
			lbl->set_text("Error: cannot load\n" + resource_path);
			gn->add_child(lbl);
		}
	} else {
		Label *lbl = memnew(Label);
		lbl->set_text("(no resource set)");
		gn->add_child(lbl);
	}

	gn->connect("position_offset_changed", callable_mp(this, &SymphonyGraphEditor::_on_node_position_changed).bind(gn->get_name()));
	gn->connect("gui_input", callable_mp(this, &SymphonyGraphEditor::_on_node_gui_input).bind(p_node.id));
	return gn;
}

void SymphonyGraphEditor::_on_resource_pick_pressed(int32_t p_node_id) {
	pending_resource_node_id = p_node_id;
	pending_subgraph_node_id = -1;
	if (!file_dialog) {
		file_dialog = memnew(FileDialog);
		file_dialog->set_file_mode(FileDialog::FILE_MODE_OPEN_FILE);
		file_dialog->set_access(FileDialog::ACCESS_RESOURCES);
		file_dialog->connect("file_selected", callable_mp(this, &SymphonyGraphEditor::_on_file_dialog_file_selected));
		add_child(file_dialog);
	}
	file_dialog->clear_filters();
	file_dialog->add_filter("*.wav", "WAV Audio");
	file_dialog->add_filter("*.tres", "Resource");
	file_dialog->set_title("Select Audio Resource");
	file_dialog->popup_centered_ratio(0.6);
}

void SymphonyGraphEditor::_on_resource_file_selected(const String &p_path) {
	// Unused — routing handled in _on_file_dialog_file_selected
}

void SymphonyGraphEditor::_on_file_dialog_file_selected(const String &p_path) {
	if (!stream.is_valid()) {
		return;
	}

	// Route: resource_path pick for an existing node
	if (pending_resource_node_id >= 0) {
		int32_t node_id = pending_resource_node_id;
		pending_resource_node_id = -1;

		GraphDescription &desc = const_cast<GraphDescription &>(stream->get_graph_description());
		for (NodeDesc &nd : desc.nodes) {
			if (nd.id == node_id) {
				nd.params["resource_path"] = p_path;
				break;
			}
		}
		stream->emit_changed();
		_rebuild_graph_edit();
		return;
	}

	// Route: SubGraph creation
	if (pending_subgraph_node_id < 0) {
		return;
	}

	NodeDesc nd;
	nd.id = pending_subgraph_node_id;
	nd.type_name = "SubGraph";
	nd.editor_position = graph_edit->get_scroll_offset() + Vector2(100, 100);
	nd.params["resource_path"] = p_path;

	pending_subgraph_node_id = -1;

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Add SubGraph Node");
	undo_redo->add_do_method(this, "_ur_add_node", _node_desc_to_dict(nd));
	undo_redo->add_undo_method(this, "_ur_remove_node", nd.id);
	undo_redo->commit_action();
}

void SymphonyGraphEditor::_on_create_new_subgraph() {
	if (!stream.is_valid()) {
		return;
	}

	// Create a new AudioStreamSymphony with a minimal graph (one GraphInput + one GraphOutput).
	Ref<AudioStreamSymphony> new_sub;
	new_sub.instantiate();

	GraphDescription sub_desc;
	NodeDesc input_nd;
	input_nd.id = 0;
	input_nd.type_name = "GraphInput";
	input_nd.params["parameter_name"] = "input";
	input_nd.params["default_value"] = 0.0f;
	input_nd.params["pin_type"] = 0.0f; // AUDIO
	input_nd.params["sort_order"] = 0.0f;
	input_nd.params["display_name"] = "input";
	input_nd.editor_position = Vector2(0, 0);
	sub_desc.nodes.push_back(input_nd);

	NodeDesc output_nd;
	output_nd.id = 1;
	output_nd.type_name = "GraphOutput";
	output_nd.params["pin_type"] = 0.0f; // AUDIO
	output_nd.params["sort_order"] = 0.0f;
	output_nd.params["display_name"] = "output";
	output_nd.editor_position = Vector2(400, 0);
	sub_desc.nodes.push_back(output_nd);

	new_sub->set_graph_description(sub_desc);

	// Ask user to save it.
	EditorNode::get_singleton()->save_resource_as(new_sub);

	// After save, if it has a path, add a SubGraph node referencing it.
	String saved_path = new_sub->get_path();
	if (!saved_path.is_empty()) {
		NodeDesc nd;
		nd.id = next_node_id++;
		nd.type_name = "SubGraph";
		nd.editor_position = graph_edit->get_scroll_offset() + Vector2(100, 100);
		nd.params["resource_path"] = saved_path;

		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action("Create New SubGraph");
		undo_redo->add_do_method(this, "_ur_add_node", _node_desc_to_dict(nd));
		undo_redo->add_undo_method(this, "_ur_remove_node", nd.id);
		undo_redo->commit_action();
	}
}

void SymphonyGraphEditor::_on_pin_type_changed(int p_index, int32_t p_node_id) {
	if (!stream.is_valid()) {
		return;
	}

	float old_value = 0.0f;
	const GraphDescription &desc = stream->get_graph_description();
	for (int i = 0; i < desc.nodes.size(); i++) {
		if (desc.nodes[i].id == p_node_id && desc.nodes[i].params.has("pin_type")) {
			old_value = desc.nodes[i].params["pin_type"];
			break;
		}
	}

	if ((int)old_value == p_index) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action("Change Pin Type");
	undo_redo->add_do_method(this, "_ur_set_param", p_node_id, StringName("pin_type"), (float)p_index);
	undo_redo->add_undo_method(this, "_ur_set_param", p_node_id, StringName("pin_type"), old_value);
	undo_redo->commit_action();

	// Rebuild the node to update slot colors/types.
	_rebuild_graph_edit();
}

// --- Breadcrumb Navigation ---

void SymphonyGraphEditor::_on_node_gui_input(const Ref<InputEvent> &p_event, int32_t p_node_id) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_double_click() && mb->get_button_index() == MouseButton::LEFT) {
		if (!stream.is_valid()) {
			return;
		}
		// Check if this node is a SubGraph.
		const GraphDescription &desc = stream->get_graph_description();
		for (const NodeDesc &nd : desc.nodes) {
			if (nd.id == p_node_id && nd.type_name == StringName("SubGraph")) {
				String resource_path;
				if (nd.params.has("resource_path")) {
					resource_path = String(nd.params["resource_path"]);
				}
				if (!resource_path.is_empty()) {
					_push_subgraph(resource_path);
				}
				break;
			}
		}
	}
}

void SymphonyGraphEditor::_push_subgraph(const String &p_resource_path) {
	Ref<AudioStreamSymphony> sub_res = ResourceLoader::load(p_resource_path, "AudioStreamSymphony");
	if (sub_res.is_null()) {
		ERR_PRINT(vformat("Cannot load SubGraph resource: %s", p_resource_path));
		return;
	}

	// Save current state to nav stack.
	NavEntry entry;
	entry.resource = stream;
	entry.scroll_offset = graph_edit->get_scroll_offset();
	nav_stack.push_back(entry);

	// Switch to the sub-graph.
	stream = sub_res;
	next_node_id = 0;
	next_frame_id = 0;
	const GraphDescription &desc = stream->get_graph_description();
	for (const NodeDesc &nd : desc.nodes) {
		if (nd.id >= next_node_id) {
			next_node_id = nd.id + 1;
		}
	}
	for (const FrameDesc &fd : desc.frames) {
		if (fd.id >= next_frame_id) {
			next_frame_id = fd.id + 1;
		}
	}

	_rebuild_graph_edit();
	_rebuild_breadcrumbs();

	// Stop preview when navigating (will restart on the new graph if user presses preview).
	if (previewing) {
		preview_player->stop();
		previewing = false;
		preview_button->set_pressed(false);
	}
}

void SymphonyGraphEditor::_navigate_to(int p_depth) {
	if (p_depth < 0 || p_depth >= nav_stack.size()) {
		return;
	}

	// Restore the target level.
	NavEntry target = nav_stack[p_depth];
	stream = target.resource;

	// Remove all entries from p_depth onward.
	nav_stack.resize(p_depth);

	// Recalculate IDs.
	next_node_id = 0;
	next_frame_id = 0;
	const GraphDescription &desc = stream->get_graph_description();
	for (const NodeDesc &nd : desc.nodes) {
		if (nd.id >= next_node_id) {
			next_node_id = nd.id + 1;
		}
	}
	for (const FrameDesc &fd : desc.frames) {
		if (fd.id >= next_frame_id) {
			next_frame_id = fd.id + 1;
		}
	}

	_rebuild_graph_edit();
	graph_edit->set_scroll_offset(target.scroll_offset);
	_rebuild_breadcrumbs();

	if (previewing) {
		preview_player->stop();
		previewing = false;
		preview_button->set_pressed(false);
	}
}

void SymphonyGraphEditor::_rebuild_breadcrumbs() {
	// Clear existing buttons.
	while (breadcrumb_bar->get_child_count() > 0) {
		Node *child = breadcrumb_bar->get_child(0);
		breadcrumb_bar->remove_child(child);
		child->queue_free();
	}

	if (nav_stack.is_empty()) {
		breadcrumb_bar->hide();
		return;
	}

	breadcrumb_bar->show();

	// Add buttons for each level in the stack + current.
	for (int i = 0; i < nav_stack.size(); i++) {
		Button *btn = memnew(Button);
		String name = nav_stack[i].resource->get_path().get_file().get_basename();
		if (name.is_empty()) {
			name = "Root";
		}
		btn->set_text(name);
		btn->set_flat(true);
		btn->connect("pressed", callable_mp(this, &SymphonyGraphEditor::_navigate_to).bind(i));
		breadcrumb_bar->add_child(btn);

		Label *sep = memnew(Label);
		sep->set_text(" > ");
		breadcrumb_bar->add_child(sep);
	}

	// Current level (non-clickable label).
	Label *current = memnew(Label);
	String current_name = stream->get_path().get_file().get_basename();
	if (current_name.is_empty()) {
		current_name = "SubGraph";
	}
	current->set_text(current_name);
	breadcrumb_bar->add_child(current);
}

// --- Save/Preview ---

void SymphonyGraphEditor::_on_save_pressed() {
	if (!stream.is_valid()) {
		return;
	}

	_sync_graph_description();

	String path = stream->get_path();
	if (path.is_empty()) {
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

	// Sync frame data (title, position, size, attached nodes).
	for (int i = 0; i < desc.frames.size(); i++) {
		StringName frame_name = _frame_name_from_id(desc.frames[i].id);
		Node *fnode = graph_edit->get_node_or_null(NodePath(String(frame_name)));
		GraphFrame *frame = Object::cast_to<GraphFrame>(fnode);
		if (frame) {
			desc.frames.write[i].title = frame->get_title();
			desc.frames.write[i].editor_position = frame->get_position_offset();
			desc.frames.write[i].editor_size = frame->get_size();
			desc.frames.write[i].tint_color = frame->get_tint_color();
		}

		TypedArray<StringName> attached = graph_edit->get_attached_nodes_of_frame(frame_name);
		desc.frames.write[i].attached_nodes.clear();
		for (int j = 0; j < attached.size(); j++) {
			int32_t nid = _node_id_from_name(attached[j]);
			if (nid >= 0) {
				desc.frames.write[i].attached_nodes.push_back(nid);
			}
		}
	}

	stream->notify_property_list_changed();
	stream->emit_changed();
}

void SymphonyGraphEditor::_recompile_and_preview() {
	if (!previewing || !stream.is_valid()) {
		return;
	}

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

	preview_player->set_stream(stream);
	preview_player->play();
}

// --- Utility ---

int32_t SymphonyGraphEditor::_node_id_from_name(const StringName &p_name) const {
	String s = String(p_name);
	if (s.begins_with("node_")) {
		return s.substr(5).to_int();
	}
	return -1;
}

StringName SymphonyGraphEditor::_name_from_node_id(int32_t p_id) const {
	return StringName(vformat("node_%d", p_id));
}

StringName SymphonyGraphEditor::_frame_name_from_id(int32_t p_id) const {
	return StringName(vformat("frame_%d", p_id));
}

int32_t SymphonyGraphEditor::_frame_id_from_name(const StringName &p_name) const {
	String s = String(p_name);
	if (s.begins_with("frame_")) {
		return s.substr(6).to_int();
	}
	return -1;
}

Dictionary SymphonyGraphEditor::_node_desc_to_dict(const NodeDesc &p_node) const {
	Dictionary d;
	d["id"] = p_node.id;
	d["type_name"] = p_node.type_name;
	d["editor_position"] = p_node.editor_position;
	d["collapsed"] = p_node.collapsed;
	Dictionary params;
	for (const KeyValue<StringName, Variant> &kv : p_node.params) {
		params[String(kv.key)] = kv.value;
	}
	d["params"] = params;
	return d;
}

NodeDesc SymphonyGraphEditor::_dict_to_node_desc(const Dictionary &p_dict) const {
	NodeDesc nd;
	nd.id = p_dict["id"];
	nd.type_name = StringName(String(p_dict["type_name"]));
	nd.editor_position = p_dict["editor_position"];
	nd.collapsed = p_dict.get("collapsed", false);
	Dictionary params = p_dict.get("params", Dictionary());
	LocalVector<Variant> keys = params.get_key_list();
	for (const Variant &key : keys) {
		nd.params[StringName(String(key))] = params[key];
	}
	return nd;
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
		if (graph_editor->get_edited_stream().is_valid()) {
			return;
		}
		if (graph_editor->is_visible_in_tree()) {
			hide_bottom_panel();
		}
		bottom_panel_button->hide();
	}
}
