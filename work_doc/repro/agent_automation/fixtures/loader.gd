extends SceneTree

func _initialize() -> void:
	_load.call_deferred()

func _load() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	for path: String in args:
		if path == "load_only":
			continue
		var resource: Resource = load(path)
		assert(resource != null)
		if resource is PackedScene and not args.has("load_only"):
			var root: Node = (resource as PackedScene).instantiate()
			assert(root != null)
			if path.ends_with("valid.tscn"):
				assert(root.get("health") == 21)
				assert(root.get("dynamic_value") == 42)
				assert(root.get("target") == root.get_node("Child"))
				assert(root.get("targets")[0] == root.get_node("Child"))
				assert(root.get("mapping")["child"] == root.get_node("Child"))
				assert(root.get_meta(&"fixture") == "roundtrip")
			root.free()
	print("STRICT LOAD DONE")
	quit()
