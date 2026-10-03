extends Node2D
@export var health: int = 10
@export var target: Node2D
@export var targets: Array[Node2D] = []
@export var mapping: Dictionary[String, Node2D] = {}
var dynamic_value: int = 0

func _init() -> void:
	print("fixture constructor executed")

func _get_property_list() -> Array[Dictionary]:
	return [{"name": "dynamic_value", "type": TYPE_INT, "usage": PROPERTY_USAGE_STORAGE}]

func _get(property: StringName) -> Variant:
	if property == &"dynamic_value":
		return dynamic_value
	return null

func _set(property: StringName, value: Variant) -> bool:
	if property == &"dynamic_value":
		dynamic_value = int(value)
		return true
	return false
