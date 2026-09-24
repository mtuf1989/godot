@tool
extends EditorPlugin
var t := 0.0
func _process(delta: float) -> void:
	t += delta
	if t < 1.0: return
	t = 0.0
	var efs := EditorInterface.get_resource_filesystem()
	if FileAccess.file_exists("res://refresh.txt"):
		for p in FileAccess.get_file_as_string("res://refresh.txt").split("\n", false):
			efs.update_file(p)
		efs.scan_sources()
		DirAccess.remove_absolute(ProjectSettings.globalize_path("res://refresh.txt"))
		print("PROBE refreshed")
	if not FileAccess.file_exists("res://probe.txt"): return
	for p in FileAccess.get_file_as_string("res://probe.txt").split("\n", false):
		var uid := ResourceLoader.get_resource_uid(p)
		var txt := ResourceUID.id_to_text(uid) if uid != -1 else "NONE"
		var reg := ResourceUID.has_id(uid) if uid != -1 else false
		var loads := ResourceLoader.exists(txt) if uid != -1 else false
		print("PROBE %s uid=%s registered=%s load_by_uid=%s" % [p, txt, reg, loads])
	print("PROBE ---")
