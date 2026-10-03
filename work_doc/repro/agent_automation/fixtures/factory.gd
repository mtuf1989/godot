extends SceneTree

func _initialize() -> void:
	_generate.call_deferred()

func _generate() -> void:
	var theme := Theme.new()
	theme.set_color(&"font_color", &"Button", Color.RED)
	var style := StyleBoxFlat.new()
	style.bg_color = Color.BLUE
	theme.set_stylebox(&"normal", &"Button", style)
	assert(ResourceSaver.save(theme, "res://theme.tres") == OK)
	var shader := Shader.new()
	shader.code = "shader_type canvas_item; uniform float strength = 0.5; void fragment() { COLOR = vec4(strength); }"
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter(&"strength", 0.75)
	assert(ResourceSaver.save(material, "res://material.tres") == OK)
	var animation := Animation.new()
	var track: int = animation.add_track(Animation.TYPE_VALUE)
	animation.track_set_path(track, NodePath("Child:position"))
	animation.track_insert_key(track, 0.0, Vector2.ZERO)
	animation.track_insert_key(track, 1.0, Vector2.ONE)
	assert(ResourceSaver.save(animation, "res://animation.tres") == OK)
	var stream := AudioStreamSymphony.new()
	stream.resource_name = "Round-trip Symphony"
	assert(ResourceSaver.save(stream, "res://symphony.tres") == OK)
	var event := SoundEvent.new()
	event.resource_name = "Round-trip SoundEvent"
	assert(ResourceSaver.save(event, "res://sound_event.tres") == OK)
	var root := Node2D.new()
	root.name = "Root"
	root.set_script(load("res://node.gd"))
	var child := Node2D.new()
	child.name = "Child"
	root.add_child(child)
	child.owner = root
	root.set("target", child)
	var targets: Array[Node2D] = [child]
	root.set("targets", targets)
	var mapping: Dictionary[String, Node2D] = {"child": child}
	root.set("mapping", mapping)
	root.set("health", 21)
	root.set("dynamic_value", 42)
	root.set_meta(&"fixture", "roundtrip")
	var scene := PackedScene.new()
	assert(scene.pack(root) == OK)
	scene.set_meta(&"asset_fixture", "metadata")
	assert(ResourceSaver.save(scene, "res://valid.tscn") == OK)
	root.free()
	print("STRICT FACTORY DONE")
	quit()
