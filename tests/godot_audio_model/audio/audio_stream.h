// CHI-101 Phase A pass-5 — minimal `AudioStream` + `AudioStreamPlayback`
// base classes for the playback side.
//
// The reference engine has a deeper hierarchy:
//
//   Resource → AudioStream → AudioStreamGenerator
//   RefCounted → AudioStreamPlayback
//              → AudioStreamPlaybackResampled
//              → AudioStreamGeneratorPlayback
//
// We flatten that for the test binary — `AudioStream` and
// `AudioStreamPlayback` are both direct RefCounted descendants here,
// and the Playback intermediate layer is skipped since godot-speech
// only calls the leaf `AudioStreamGeneratorPlayback`'s API.

#pragma once

#include "../core/core_types.h"

class AudioStream : public RefCounted {
public:
	virtual ~AudioStream() = default;
};

class AudioStreamPlayback : public RefCounted {
public:
	virtual ~AudioStreamPlayback() = default;
};
