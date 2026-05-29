extends Node
## Phase 2 Validation: Records Symphony output to WAV for waveform inspection.
## Expected: first 35 samples are silence (envelope IDLE), then attack ramp begins.

@onready var player: AudioStreamPlayer = $AudioStreamPlayer

var record_effect: AudioEffectRecord
var recording_started := false
var frames_waited := 0


func _ready() -> void:
	# Create a "Record" bus with AudioEffectRecord
	var bus_idx := AudioServer.get_bus_index("Record")
	if bus_idx == -1:
		AudioServer.add_bus()
		bus_idx = AudioServer.bus_count - 1
		AudioServer.set_bus_name(bus_idx, "Record")
		AudioServer.set_bus_send(bus_idx, "Master")

	record_effect = AudioEffectRecord.new()
	record_effect.format = AudioStreamWAV.FORMAT_16_BITS
	AudioServer.add_bus_effect(bus_idx, record_effect)

	# Start recording, then play
	record_effect.set_recording_active(true)
	recording_started = true
	player.play()
	print("Phase 2: Recording started. Trigger at sample offset 35.")
	print("Will record for 1 second then save WAV.")


func _process(delta: float) -> void:
	if not recording_started:
		return

	frames_waited += 1
	# Wait ~1 second (60 frames at 60fps) then stop and save
	if frames_waited >= 60:
		record_effect.set_recording_active(false)
		player.stop()

		var recording := record_effect.get_recording()
		if recording:
			var path := "user://symphony_phase2_test.wav"
			var err := recording.save_to_wav(path)
			if err == OK:
				print("WAV saved to: ", path)
				print("Open in Audacity and verify:")
				print("  - First 35 samples should be ~0 (silence)")
				print("  - Sample 36+ should show attack ramp * sine wave")
			else:
				print("ERROR: Failed to save WAV: ", err)
		else:
			print("ERROR: No recording data")

		get_tree().quit()
