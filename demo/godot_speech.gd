extends Speech

var blank_packet: PackedVector2Array = PackedVector2Array()
var player_audio: Dictionary = {}

@export  var use_sample_stretching : bool = true
var Xuse_sample_stretching : bool = false

const BUFFER_DELAY_THRESHOLD = 0.1

const STREAM_STANDARD_PITCH = 1.0
const STREAM_SPEEDUP_PITCH = 1.5

const MAX_JITTER_BUFFER_SIZE = 16
const JITTER_BUFFER_SPEEDUP = 12
const JITTER_BUFFER_SLOWDOWN = 6

const DEBUG = false

var uncompressed_audio: PackedVector2Array = PackedVector2Array()

# Debugging info
var packets_received_this_frame: int = 0
var playback_ring_buffer_length: int = 0

class PlaybackStats:
	var playback_ring_current_size: int = 0
	var playback_ring_max_size: int = 0
	var playback_ring_size_sum: float = 0.0
	var playback_get_frames: float = 0.0
	var playback_pushed_calls: int = 0
	var playback_discarded_calls: int = 0
	var playback_push_buffer_calls: int = 0
	var playback_blank_push_calls: int = 0
	var playback_position: float = 0.0
	var playback_skips: float = 0.0

	var jitter_buffer_size_sum: float = 0.0
	var jitter_buffer_calls: int = 0
	var jitter_buffer_max_size: int = 0
	var jitter_buffer_current_size: int = 0

	var playback_ring_buffer_length: int = 0
	var buffer_frame_count: int = 0

	func get_playback_stats(outerscope) -> Dictionary:
		var playback_pushed_frames: float = playback_pushed_calls * (buffer_frame_count * 1.0)
		var playback_discarded_frames: float = playback_discarded_calls * (buffer_frame_count * 1.0)
		return {
		"playback_ring_limit_s": playback_ring_buffer_length / float(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE),
		"playback_ring_current_size_s": playback_ring_current_size / float(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE),
		"playback_ring_max_size_s": playback_ring_max_size / float(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE),
		"playback_ring_mean_size_s": playback_ring_size_sum / playback_push_buffer_calls / float(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE),
		"jitter_buffer_current_size_s": float(jitter_buffer_current_size) * SpeechProcessor.SPEECH_SETTING_PACKET_DELTA_TIME,
		"jitter_buffer_max_size_s": float(jitter_buffer_max_size) * SpeechProcessor.SPEECH_SETTING_PACKET_DELTA_TIME,
		"jitter_buffer_mean_size_s": float(jitter_buffer_size_sum) / jitter_buffer_calls * SpeechProcessor.SPEECH_SETTING_PACKET_DELTA_TIME,
		"jitter_buffer_calls": jitter_buffer_calls,
		"playback_position_s": playback_position,
		"playback_get_percent": 100.0 * playback_get_frames / playback_pushed_frames,
		"playback_discard_percent": 100.0 * playback_discarded_frames / playback_pushed_frames,
		"playback_get_s": playback_get_frames / float(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE),
		"playback_pushed_s": playback_pushed_frames / float(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE),
		"playback_discarded_s": playback_discarded_frames / float(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE),
		"playback_push_buffer_calls": floor(playback_push_buffer_calls),
		#"playback_blank_push_calls": floor(playback_blank_push_calls),
		"playback_blank_s": playback_blank_push_calls * SpeechProcessor.SPEECH_SETTING_PACKET_DELTA_TIME,
		"playback_blank_percent": 100.0 * playback_blank_push_calls / playback_push_buffer_calls,
		"playback_skips": floor(playback_skips),
		}


func nearest_shift(p_number: int) -> int:
	for i in range(30, -1, -1):
		if (p_number & (1 << i)):
			return i + 1

	return 0


func calc_playback_ring_buffer_length(audio_stream_generator: AudioStreamGenerator) -> int:
	var target_buffer_size : int = int(audio_stream_generator.mix_rate * audio_stream_generator.buffer_length);
	return (1 << nearest_shift(target_buffer_size));


func get_playback_stats(speech_statdict: Dictionary) -> Dictionary:
	var statdict = {}
	for skey in speech_statdict:
		statdict[str(skey)] = (speech_statdict[skey])
	statdict["capture_get_percent"] = 100.0 * statdict["capture_get_s"] / statdict["capture_pushed_s"]
	statdict["capture_discard_percent"] = 100.0 * statdict["capture_discarded_s"] / statdict["capture_pushed_s"]
	for key in player_audio.keys():
		statdict[key] = player_audio[key]["playback_stats"].get_playback_stats(self)
		#statdict[key]["playback_prev_ticks"] = player_audio[key]["playback_prev_time"] / float(SpeechProcessor.SPEECH_SETTING_MILLISECONDS_PER_SECOND)
		#statdict[key]["playback_start_ticks"] = player_audio[key]["playback_start_time"] / float(SpeechProcessor.SPEECH_SETTING_MILLISECONDS_PER_SECOND)
		statdict[key]["playback_total_time"] = (Time.get_ticks_msec() - player_audio[key]["playback_start_time"]) / float(SpeechProcessor.SPEECH_SETTING_MILLISECONDS_PER_SECOND)
		statdict[key]["excess_packets"] = player_audio[key]["excess_packets"]
		statdict[key]["excess_s"] = player_audio[key]["excess_packets"] * SpeechProcessor.SPEECH_SETTING_PACKET_DELTA_TIME
	return statdict


func vc_debug_print(p_str):
	if not DEBUG:
		return
	print(p_str)


func vc_debug_printerr(p_str):
	if not DEBUG:
		return
	printerr(p_str)


func add_player_audio(p_player_id: int, p_audio_stream_player: Node) -> void:
	if (
		p_audio_stream_player is AudioStreamPlayer
		or p_audio_stream_player is AudioStreamPlayer2D
		or p_audio_stream_player is AudioStreamPlayer3D
	):
		if ! player_audio.has(p_player_id):
			var new_generator: AudioStreamGenerator = AudioStreamGenerator.new()
			new_generator.set_mix_rate(SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE)
			new_generator.set_buffer_length(BUFFER_DELAY_THRESHOLD)
			playback_ring_buffer_length = calc_playback_ring_buffer_length(new_generator)

			p_audio_stream_player.set_stream(new_generator)
			p_audio_stream_player.bus = "VoiceOutput"
			p_audio_stream_player.autoplay = true
			p_audio_stream_player.play()

			var speech_decoder: RefCounted = get_speech_decoder()

			var pstats = PlaybackStats.new()
			pstats.playback_ring_buffer_length = playback_ring_buffer_length
			pstats.buffer_frame_count = SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT
			player_audio[p_player_id] = {
				"audio_stream_player": p_audio_stream_player,
				"jitter_buffer": [],
				"sequence_id": -1,
				"last_update": Time.get_ticks_msec(),
				"packets_received_this_frame": 0,
				"excess_packets": 0,
				"speech_decoder": speech_decoder,
				"playback_stats": pstats,
				"playback_start_time": 0,
				"playback_prev_time": -1,
				"playback_last_skips": 0,
			}
		else:
			printerr("Attempted to duplicate player_audio entry (%s)!" % p_player_id)


func remove_player_audio(p_player_id: int) -> void:
	if player_audio.has(p_player_id):
		if player_audio.erase(p_player_id):
			return

	printerr("Attempted to remove non-existant player_audio entry (%s)" % p_player_id)


func clear_all_player_audio() -> void:
	for key in player_audio.keys():
		if player_audio[key]["audio_stream_player"]:
			player_audio[key]["audio_stream_player"].queue_free()

	player_audio = {}

func on_received_audio_packet(p_peer_id: int, p_sequence_id: int, p_packet: PackedByteArray) -> void:
	vc_debug_print(
		"received_audio_packet: peer_id: {id} sequence_id: {sequence_id}".format(
			{"id": str(p_peer_id), "sequence_id": str(p_sequence_id)}
		)
	)

	if not player_audio.has(p_peer_id):
		return

	# Detects if no audio packets have been received from this player yet.
	if player_audio[p_peer_id]["sequence_id"] == -1:
		player_audio[p_peer_id]["sequence_id"] = p_sequence_id - 1

	player_audio[p_peer_id]["packets_received_this_frame"] += 1
	packets_received_this_frame += 1

	var current_sequence_id: int = player_audio[p_peer_id]["sequence_id"]
	var jitter_buffer: Array = player_audio[p_peer_id]["jitter_buffer"]

	var sequence_id_offset: int = p_sequence_id - current_sequence_id
	if sequence_id_offset > 0:
		# For skipped buffers, add empty packets
		var skipped_packets = sequence_id_offset - 1
		if skipped_packets:
			var fill_packets = null

			# If using stretching, fill with last received packet
			if Xuse_sample_stretching and jitter_buffer.size() > 0:
				fill_packets = jitter_buffer.back()["packet"]

			for _i in range(0, skipped_packets):
				jitter_buffer.push_back({"packet": fill_packets, "valid": false})
		# Add the new valid buffer
		jitter_buffer.push_back({"packet": p_packet, "valid": true})

		var excess_packet_count: int = jitter_buffer.size() - MAX_JITTER_BUFFER_SIZE
		if excess_packet_count > 0:
			# print("Excess packet count: %s" % str(excess_packet_count))
			for _i in range(0, excess_packet_count):
				player_audio[p_peer_id]["excess_packets"] += 1
				jitter_buffer.pop_front()

		player_audio[p_peer_id]["sequence_id"] = player_audio[p_peer_id]["sequence_id"] + sequence_id_offset
	else:
		var sequence_id: int = jitter_buffer.size() - 1 + sequence_id_offset
		vc_debug_print("Updating existing sequence_id: %s" % str(sequence_id))
		if sequence_id >= 0:
			# Update existing buffer
			if Xuse_sample_stretching:
				var jitter_buffer_size = jitter_buffer.size()
				for i in range(sequence_id, jitter_buffer_size - 1):
					if jitter_buffer[i]["valid"]:
						break

					jitter_buffer[i] = {"packet": p_packet, "valid": false}

			jitter_buffer[sequence_id] = {"packet": p_packet, "valid": true}
		else:
			vc_debug_printerr("invalid repair sequence_id!")

	player_audio[p_peer_id]["jitter_buffer"] = jitter_buffer


func attempt_to_feed_stream(
	p_skip_count: int, p_decoder: RefCounted, p_audio_stream_player: Node, p_jitter_buffer: Array, p_playback_stats: PlaybackStats, p_player_dict: Dictionary
) -> void:
	if p_audio_stream_player == null:
		return

	for _i in range(0, p_skip_count):
		p_jitter_buffer.pop_front()

	var playback = p_audio_stream_player.get_stream_playback()
	if playback == null:
		return
	if p_player_dict["playback_last_skips"] != playback.get_skips():
		p_player_dict["playback_prev_time"] = p_player_dict["playback_prev_time"] - SpeechProcessor.MILLISECONDS_PER_PACKET
		p_player_dict["playback_last_skips"] = playback.get_skips()

	var to_fill: int = playback.get_frames_available()
	var required_packets: int = 0
	while to_fill >= SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT:
		to_fill -= SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT
		required_packets += 1

	var last_packet = null
	if p_jitter_buffer.size() > 0:
		last_packet = p_jitter_buffer.back()["packet"]
	while p_jitter_buffer.size() < required_packets:
		var fill_packets = null
		# If using stretching, fill with last received packet
		if Xuse_sample_stretching and p_jitter_buffer.size() > 0:
			fill_packets = last_packet

		p_jitter_buffer.push_back({"packet": fill_packets, "valid": false})

	for _i in range(0, required_packets):
		var packet = p_jitter_buffer.pop_front()
		var packet_pushed: bool = false
		var push_result: bool = false
		if packet:
			var buffer = packet["packet"]
			if buffer:
				uncompressed_audio = decompress_buffer(
					p_decoder, buffer, buffer.size(), uncompressed_audio
				)
				if uncompressed_audio:
					if uncompressed_audio.size() == SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT:
						push_result =  playback.push_buffer(uncompressed_audio)
						packet_pushed = true
		if !packet_pushed:
			push_result = playback.push_buffer(blank_packet)

		p_playback_stats.playback_ring_current_size = playback_ring_buffer_length - playback.get_frames_available()
		p_playback_stats.playback_ring_max_size = p_playback_stats.playback_ring_current_size if p_playback_stats.playback_ring_current_size > p_playback_stats.playback_ring_max_size else p_playback_stats.playback_ring_max_size
		p_playback_stats.playback_ring_size_sum += 1.0 * p_playback_stats.playback_ring_current_size
# TODO: iFire 2021-10-22 Submit upstream
#		p_playback_stats.playback_position = playback.get_playback_position()
#		p_playback_stats.playback_get_frames = playback.get_playback_position() * SpeechProcessor.SPEECH_SETTING_VOICE_SAMPLE_RATE
		p_playback_stats.playback_push_buffer_calls += 1
		if ! packet_pushed:
			p_playback_stats.playback_blank_push_calls += 1
		if push_result:
			p_playback_stats.playback_pushed_calls += 1
		else:
			p_playback_stats.playback_discarded_calls += 1
		p_playback_stats.playback_skips = 1.0 * float(playback.get_skips())

	if Xuse_sample_stretching and p_jitter_buffer.size() == 0:
		p_jitter_buffer.push_back({"packet": last_packet, "valid": false})

	p_playback_stats.jitter_buffer_size_sum += p_jitter_buffer.size()
	p_playback_stats.jitter_buffer_calls += 1
	p_playback_stats.jitter_buffer_max_size = p_jitter_buffer.size() if p_jitter_buffer.size() > p_playback_stats.jitter_buffer_max_size else p_playback_stats.jitter_buffer_max_size
	p_playback_stats.jitter_buffer_current_size = p_jitter_buffer.size()

	# Speed up or slow down the audio stream to mitigate skipping
	if p_jitter_buffer.size() > JITTER_BUFFER_SPEEDUP:
		p_audio_stream_player.pitch_scale = STREAM_SPEEDUP_PITCH
	elif p_jitter_buffer.size() < JITTER_BUFFER_SLOWDOWN:
		p_audio_stream_player.pitch_scale = STREAM_STANDARD_PITCH

func _process(_delta: float) -> void:
	if player_audio == null:
		return
	for key in player_audio:
		if not key:
			continue
		var elem: Dictionary = player_audio[key]
		if not elem.has("speech_decoder"):
			continue
		var speech_decoder = elem["speech_decoder"]
		if not elem.has("audio_stream_player"):
			continue
		var audio_stream_player = elem["audio_stream_player"]
		if not elem.has("jitter_buffer"):
			continue
		var jitter_buffer = elem["jitter_buffer"]
		if not elem.has("playback_stats"):
			continue
		var playback_stats = elem["playback_stats"]
		attempt_to_feed_stream(
			0,
			speech_decoder,
			audio_stream_player,
			jitter_buffer,
			playback_stats,
			elem
		)
		player_audio[key]["packets_received_this_frame"] = 0
	packets_received_this_frame = 0


func _ready() -> void:
	uncompressed_audio.resize(SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT)


func _init():
	blank_packet.resize(SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT)
	for i in range(0, SpeechProcessor.SPEECH_SETTING_BUFFER_FRAME_COUNT):
		blank_packet[i] = Vector2(0.0, 0.0)
