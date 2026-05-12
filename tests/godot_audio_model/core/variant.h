// CHI-101 Phase A pass-6 — minimal `Variant` stand-in.
//
// The reference engine's Variant is a tagged union over ~30 types
// with full reflection support. godot-speech uses it through a
// much narrower keyhole — the only call sites in speech.cpp /
// speech_processor.cpp are:
//
//   p_audio_stream_player->call("play", get_playback_position())
//     → Variant(float) argument; return value discarded.
//   Ref<AudioStreamGeneratorPlayback> pb = p_audio_stream_player->call("get_stream_playback")
//     → Variant holding a Ref<RefCounted>; implicit conversion
//       to Ref<DerivedT> via dynamic_cast.
//   Dictionary["…"] = …, int64_t(Dict["…"]), bool(Dict["…"])
//     → primitive get/set on dictionary values; needs to round
//       trip int / bool / double / String / Ref.
//
// We support that keyhole and nothing more. The Variant here is
// not an active union — it stores every primitive type as separate
// fields (small constant overhead per Variant, easier to reason
// about than placement-new tagged-union juggling).

#pragma once

#include "ref_counted.h"
#include "string.h"
#include "typedefs.h"

class Variant {
public:
	enum Type {
		NIL,
		BOOL,
		INT,
		FLOAT,
		STRING,
		OBJECT,
	};

private:
	Type type = NIL;
	int64_t i = 0;
	double f = 0.0;
	bool b = false;
	String s;
	Ref<RefCounted> o;

public:
	Variant() = default;
	Variant(bool v) :
			type(BOOL), b(v) {}
	Variant(int v) :
			type(INT), i(v) {}
	Variant(int64_t v) :
			type(INT), i(v) {}
	Variant(uint32_t v) :
			type(INT), i(static_cast<int64_t>(v)) {}
	Variant(float v) :
			type(FLOAT), f(v) {}
	Variant(double v) :
			type(FLOAT), f(v) {}
	Variant(const char *v) :
			type(STRING), s(v) {}
	Variant(const String &v) :
			type(STRING), s(v) {}

	// Accept any Ref<T> where T derives from RefCounted.
	template <typename T>
	Variant(const Ref<T> &v) :
			type(OBJECT) {
		if (v.is_valid()) {
			o = Ref<RefCounted>(static_cast<RefCounted *>(v.ptr_raw()));
		}
	}

	Type get_type() const { return type; }

	// Implicit primitive conversions.
	operator bool() const {
		switch (type) {
			case BOOL:
				return b;
			case INT:
				return i != 0;
			case FLOAT:
				return f != 0.0;
			case OBJECT:
				return o.is_valid();
			default:
				return false;
		}
	}
	operator int() const { return static_cast<int>(i); }
	operator int64_t() const { return i; }
	operator uint32_t() const { return static_cast<uint32_t>(i); }
	operator float() const { return static_cast<float>(f); }
	operator double() const { return f; }
	operator String() const { return s; }

	// Implicit Ref<T> conversion via dynamic_cast through the
	// stored RefCounted*. Mirrors the engine's behavior: the
	// Variant ignores type mismatches and returns an empty Ref.
	template <typename T>
	operator Ref<T>() const {
		if (type != OBJECT || o.is_null()) {
			return Ref<T>();
		}
		T *casted = dynamic_cast<T *>(o.ptr_raw());
		return Ref<T>(casted);
	}
};
