extends Control
## Phase 5 Stress Test: Spawns N AudioStreamPlayers with configurable graph complexity.
## HUD shows: active voices, budget %, per-voice avg µs, arena memory.
## Controls: Up/Down = add/remove voices, 1/2/3 = tier select, Escape = quit.

@export var initial_voices: int = 10
@export var ramp_interval: float = 0.5  # seconds between auto-ramp spawns

var voice_manager: Object
var players: Array[AudioStreamPlayer] = []
var current_tier: int = 1
var hud_label: Label


func _ready() -> void:
	voice_manager = Engine.get_singleton("SymphonyVoiceManager")

	hud_label = Label.new()
	hud_label.position = Vector2(10, 10)
	hud_label.add_theme_font_size_override("font_size", 16)
	add_child(hud_label)

	for i in initial_voices:
		_spawn_voice(current_tier)

	print("Phase 5 Stress Test started. Tier %d, %d voices." % [current_tier, initial_voices])
	print("  Up/Down = add/remove voice | 1/2/3 = tier | Escape = quit")


func _process(_delta: float) -> void:
	if Input.is_action_just_pressed("ui_cancel"):
		_stop_all()
		get_tree().quit()
		return

	if Input.is_action_just_pressed("ui_up"):
		for i in 10:
			_spawn_voice(current_tier)
	if Input.is_action_just_pressed("ui_down"):
		_remove_voice()
	if Input.is_key_pressed(KEY_1):
		current_tier = 1
	if Input.is_key_pressed(KEY_2):
		current_tier = 2
	if Input.is_key_pressed(KEY_3):
		current_tier = 3

	# Update HUD
	var count: int = voice_manager.get_active_voice_count() if voice_manager else players.size()
	var budget: float = voice_manager.get_total_budget_percent() if voice_manager else 0.0
	var avg_us: float = voice_manager.get_average_voice_microseconds() if voice_manager else 0.0
	hud_label.text = "Tier: %d | Voices: %d | Budget: %.1f%% | Avg: %.0f µs" % [
		current_tier, count, budget, avg_us
	]


func _spawn_voice(p_tier: int) -> void:
	var player := AudioStreamPlayer.new()
	var stream := AudioStreamSymphony.new()
	stream.mix_rate = 44100.0
	stream.voice_priority = randi_range(10, 90)

	# Build graph programmatically based on tier
	match p_tier:
		1:
			_build_small_graph(stream)
		2:
			if randf() > 0.5:
				_build_medium_graph(stream)
			else:
				_build_small_graph(stream)
		3:
			var r := randf()
			if r < 0.33:
				_build_small_graph(stream)
			elif r < 0.66:
				_build_medium_graph(stream)
			else:
				_build_large_graph(stream)

	player.stream = stream
	add_child(player)
	player.play()
	players.append(player)


func _remove_voice() -> void:
	if players.is_empty():
		return
	var p: AudioStreamPlayer = players.pop_back()
	p.stop()
	p.queue_free()


func _stop_all() -> void:
	for p in players:
		p.stop()
		p.queue_free()
	players.clear()


## Small graph (10 nodes): 2x Osc + filters + ADSR -> GraphOutput
func _build_small_graph(stream: AudioStreamSymphony) -> void:
	stream.load_test_graph()


## Medium graph (30 nodes): 3 voices + LFO modulation + compressor chain
func _build_medium_graph(stream: AudioStreamSymphony) -> void:
	stream.load_test_graph_30()


## Large graph (50 nodes): 5 voices with dual filter stages + master chain
func _build_large_graph(stream: AudioStreamSymphony) -> void:
	stream.load_test_graph_50()
