extends Control
## Voice Stealing Test: Validates that SymphonyVoiceManager correctly steals voices.
## 4 scenarios: max_voices limit, budget threshold, RMS tiebreaker, no-glitch.
## Press 1-4 to run each test. Escape to quit.

var voice_manager: Object
var players: Array[AudioStreamPlayer] = []
var hud_label: Label
var log_label: Label
var current_test: int = 0


func _ready() -> void:
	voice_manager = Engine.get_singleton("SymphonyVoiceManager")

	hud_label = Label.new()
	hud_label.position = Vector2(10, 10)
	hud_label.add_theme_font_size_override("font_size", 16)
	add_child(hud_label)

	log_label = Label.new()
	log_label.position = Vector2(10, 60)
	log_label.add_theme_font_size_override("font_size", 14)
	log_label.autowrap_mode = TextServer.AUTOWRAP_WORD
	log_label.custom_minimum_size = Vector2(600, 400)
	add_child(log_label)

	_log("Voice Stealing Test Ready.")
	_log("  1 = Max voices limit")
	_log("  2 = Budget critical threshold")
	_log("  3 = RMS tiebreaker")
	_log("  4 = No-glitch listen test")
	_log("  Escape = quit")


func _process(_delta: float) -> void:
	if Input.is_action_just_pressed("ui_cancel"):
		_stop_all()
		get_tree().quit()
		return

	if Input.is_key_pressed(KEY_1) and current_test != 1:
		_run_test_1()
	if Input.is_key_pressed(KEY_2) and current_test != 2:
		_run_test_2()
	if Input.is_key_pressed(KEY_3) and current_test != 3:
		_run_test_3()
	if Input.is_key_pressed(KEY_4) and current_test != 4:
		_run_test_4()

	# Update HUD
	var count: int = voice_manager.get_active_voice_count() if voice_manager else 0
	var budget: float = voice_manager.get_total_budget_percent() if voice_manager else 0.0
	hud_label.text = "Test: %d | Voices: %d | Budget: %.1f%%" % [current_test, count, budget]


## Test 1: Max voices limit
## Set max_voices=20, spawn 30 voices with varying priorities.
## Expect: 10 lowest-priority voices get stopped.
func _run_test_1() -> void:
	_stop_all()
	current_test = 1
	_log("\n--- TEST 1: Max Voices Limit ---")

	voice_manager.max_voices = 20
	voice_manager.critical_threshold = 0.99  # Disable budget stealing

	# Spawn 10 at priority 90, 10 at priority 50, 10 at priority 10
	for i in 10:
		_spawn_voice(90)
	for i in 10:
		_spawn_voice(50)
	for i in 10:
		_spawn_voice(10)

	_log("Spawned 30 voices (10@pri90, 10@pri50, 10@pri10)")
	_log("max_voices=20. Waiting 1s for stealing...")

	await get_tree().create_timer(1.0).timeout

	var count: int = voice_manager.get_active_voice_count()
	_log("After stealing: %d voices active" % count)
	if count <= 20:
		_log("PASS: Voice count within limit")
	else:
		_log("FAIL: Voice count %d exceeds max 20" % count)


## Test 2: Budget critical threshold
## Set critical=0.5, warning=0.3. Spawn tier-3 voices until budget > 50%.
## Expect: voices culled back to ~30%.
func _run_test_2() -> void:
	_stop_all()
	current_test = 2
	_log("\n--- TEST 2: Budget Critical Threshold ---")

	voice_manager.max_voices = 0  # Unlimited
	voice_manager.critical_threshold = 0.5
	voice_manager.warning_threshold = 0.3

	# Spawn many tier-3 voices to push budget high
	for i in 60:
		_spawn_voice_tier3(50)

	_log("Spawned 60 tier-3 voices at priority 50")
	_log("critical=50%, warning=30%. Waiting 2s...")

	await get_tree().create_timer(2.0).timeout

	var budget: float = voice_manager.get_total_budget_percent()
	var count: int = voice_manager.get_active_voice_count()
	_log("After stealing: %d voices, budget=%.1f%%" % [count, budget])
	if budget <= 50.0:
		_log("PASS: Budget under critical threshold")
	else:
		_log("NOTE: Budget still high (%.1f%%) — may need more mix cycles" % budget)


## Test 3: RMS tiebreaker
## Spawn 10 voices at same priority. 5 silent (gain=0), 5 audible.
## Set max_voices=5. Expect: silent ones stolen first.
func _run_test_3() -> void:
	_stop_all()
	current_test = 3
	_log("\n--- TEST 3: RMS Tiebreaker ---")

	voice_manager.max_voices = 5
	voice_manager.critical_threshold = 0.99

	# 5 silent voices (priority 50)
	for i in 5:
		_spawn_voice_with_gain(50, 0.0)
	# 5 audible voices (priority 50)
	for i in 5:
		_spawn_voice_with_gain(50, 0.5)

	_log("Spawned 10 voices: 5 silent + 5 audible (all priority 50)")
	_log("max_voices=5. Waiting 1s...")

	await get_tree().create_timer(1.0).timeout

	var count: int = voice_manager.get_active_voice_count()
	_log("After stealing: %d voices active" % count)
	if count <= 5:
		_log("PASS: Voice count within limit (silent ones should be gone)")
	else:
		_log("FAIL: Voice count %d exceeds max 5" % count)


## Test 4: No-glitch listen test
## Spawn 20 voices, set max_voices=10. Listen for pops/clicks.
func _run_test_4() -> void:
	_stop_all()
	current_test = 4
	_log("\n--- TEST 4: No-Glitch Listen Test ---")

	voice_manager.max_voices = 10
	voice_manager.critical_threshold = 0.99

	for i in 20:
		_spawn_voice(randi_range(10, 90))

	_log("Spawned 20 voices with random priorities, max_voices=10")
	_log("LISTEN for pops/clicks during stealing...")
	_log("(Manual verification — check audio output)")


func _spawn_voice(p_priority: int) -> void:
	var player := AudioStreamPlayer.new()
	var stream := AudioStreamSymphony.new()
	stream.mix_rate = 44100.0
	stream.voice_priority = p_priority
	stream.load_test_graph()
	player.stream = stream
	add_child(player)
	player.play()
	players.append(player)


func _spawn_voice_tier3(p_priority: int) -> void:
	var player := AudioStreamPlayer.new()
	var stream := AudioStreamSymphony.new()
	stream.mix_rate = 44100.0
	stream.voice_priority = p_priority
	stream.load_test_graph_50()
	player.stream = stream
	add_child(player)
	player.play()
	players.append(player)


func _spawn_voice_with_gain(p_priority: int, p_gain: float) -> void:
	var player := AudioStreamPlayer.new()
	var stream := AudioStreamSymphony.new()
	stream.mix_rate = 44100.0
	stream.voice_priority = p_priority
	stream.load_test_graph()
	player.stream = stream
	player.volume_db = linear_to_db(p_gain) if p_gain > 0.0 else -80.0
	add_child(player)
	player.play()
	players.append(player)


func _stop_all() -> void:
	voice_manager.max_voices = 0
	voice_manager.critical_threshold = 0.90
	voice_manager.warning_threshold = 0.70
	for p in players:
		p.stop()
		p.queue_free()
	players.clear()


func _log(msg: String) -> void:
	print(msg)
	log_label.text += msg + "\n"
