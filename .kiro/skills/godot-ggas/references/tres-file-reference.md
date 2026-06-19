# .tres File Reference

Examples of how .tres files for ggas look like

## GameplayTagResource

```ini
; res://data/tags/status_burning.tres
[gd_resource type="Resource" script_class="GameplayTagResource" load_steps=2 format=3]

[ext_resource type="Script" path="res://addons/ggas/resources/gameplay_tag_resource.gd" id="1"]

[resource]
script = ExtResource("1")
tag_name = "Status.Debuff.Burning"
description = "Entity is on fire, taking periodic damage"
color = Color(1, 0.3, 0, 1)
```

## GameplayEffectModifier (Sub-Resource)

Modifiers are typically embedded as sub-resources inside a GameplayEffectResource:

```ini
[sub_resource type="Resource" id="mod_1"]
script = ExtResource("2")
attribute_name = "health"
operation = 0
magnitude_calculation = 0
magnitude_value = -25.0
level_multiplier = 0.0
```

## GameplayEffectResource (Instant Damage)

```ini
; res://data/effects/fire_damage.tres
[gd_resource type="Resource" script_class="GameplayEffectResource" load_steps=4 format=3]

[ext_resource type="Script" path="res://addons/ggas/resources/gameplay_effect_resource.gd" id="1"]
[ext_resource type="Script" path="res://addons/ggas/resources/gameplay_effect_modifier.gd" id="2"]
[ext_resource type="Resource" path="res://data/tags/status_burning.tres" id="3"]

[sub_resource type="Resource" id="mod_1"]
script = ExtResource("2")
attribute_name = "health"
operation = 0
magnitude_calculation = 0
magnitude_value = -25.0

[resource]
script = ExtResource("1")
duration_policy = 0
modifiers = [SubResource("mod_1")]
required_tags = [ExtResource("3")]
```

## GameplayEffectResource (Duration Buff)

```ini
; res://data/effects/haste_buff.tres
[gd_resource type="Resource" script_class="GameplayEffectResource" load_steps=4 format=3]

[ext_resource type="Script" path="res://addons/ggas/resources/gameplay_effect_resource.gd" id="1"]
[ext_resource type="Script" path="res://addons/ggas/resources/gameplay_effect_modifier.gd" id="2"]
[ext_resource type="Resource" path="res://data/tags/status_hasted.tres" id="3"]

[sub_resource type="Resource" id="mod_1"]
script = ExtResource("2")
attribute_name = "movement_speed"
operation = 1
magnitude_calculation = 0
magnitude_value = 0.3

[resource]
script = ExtResource("1")
duration_policy = 2
duration_magnitude = 10.0
modifiers = [SubResource("mod_1")]
granted_tags = [ExtResource("3")]
allow_stacking = true
max_stack_count = 3
refresh_duration_on_stack = true
```

## AttributeSetResource

```ini
; res://data/attributes/enemy_attributes.tres
[gd_resource type="Resource" script_class="AttributeSetResource" load_steps=2 format=3]

[ext_resource type="Script" path="res://addons/ggas/resources/attribute_set_resource.gd" id="1"]

[resource]
script = ExtResource("1")
attributes = {
"health": 200.0,
"max_health": 200.0,
"attack_power": 15.0,
"defense": 8.0,
"movement_speed": 0.8
}
```