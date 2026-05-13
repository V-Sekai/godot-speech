// CHI-101 Phase A pass-3 — minimal String / StringName / NodePath
// stand-ins. The reference engine has rich `core/string/ustring.h`
// + StringName interning + COW semantics; we just wrap std::string.
//
// Trade-offs vs the engine:
//   * No reference counting — every String owns its bytes.
//   * No interning for StringName — equality is byte-wise.
//   * Encoding is assumed UTF-8 throughout; no UTF-16 / UCS-4 paths.
//
// This is fine for the test binary because every String it sees is
// either a literal (`"Default"`, `"AudioStreamPlayer"`, etc.) or a
// device name passed through from CoreAudio.

#pragma once

#include "typedefs.h"

#include <cstring>
#include <string>
#include <vector>

class CharString {
	std::string s;

public:
	CharString() = default;
	CharString(const std::string &p_s) :
			s(p_s) {}
	CharString(const char *p_s) :
			s(p_s ? p_s : "") {}

	const char *get_data() const { return s.c_str(); }
	int length() const { return static_cast<int>(s.size()); }
	int size() const { return static_cast<int>(s.size()) + 1; } // +1 for NUL
};

class String {
	std::string s;

public:
	String() = default;
	String(const char *p_s) :
			s(p_s ? p_s : "") {}
	String(const std::string &p_s) :
			s(p_s) {}

	const char *ptr() const { return s.c_str(); }
	int length() const { return static_cast<int>(s.size()); }
	bool is_empty() const { return s.empty(); }
	const std::string &std_str() const { return s; }

	CharString utf8() const { return CharString(s); }
	CharString ascii() const { return CharString(s); }

	// Engine factory: `String::utf8("…")`. Reproduces enough for the
	// CoreAudio device-name path. No validation; bytes are taken as-is.
	static String utf8(const char *p_str, int /*p_len*/ = -1) {
		return String(p_str ? p_str : "");
	}

	bool operator==(const String &o) const { return s == o.s; }
	bool operator!=(const String &o) const { return s != o.s; }
	bool operator<(const String &o) const { return s < o.s; }

	String operator+(const String &o) const {
		String r;
		r.s = s + o.s;
		return r;
	}
	String &operator+=(const String &o) {
		s += o.s;
		return *this;
	}
};

// `itos` mirrors Godot's int-to-String helper. Linux's GCC aliases
// `int64_t` to `long` (not `long long`), and macOS's clang aliases
// it to `long long` — both overloads need to exist to resolve
// without ambiguity across platforms.
inline String itos(long long v) {
	return String(std::to_string(v));
}
inline String itos(long v) {
	return String(std::to_string(v));
}
inline String itos(int v) {
	return String(std::to_string(v));
}
inline String itos(unsigned int v) {
	return String(std::to_string(v));
}
inline String itos(unsigned long v) {
	return String(std::to_string(v));
}
inline String itos(unsigned long long v) {
	return String(std::to_string(v));
}

// `const char* + String` for log-concat call sites like
// `print_verbose("CoreAudio: " + itos(n))`.
inline String operator+(const char *lhs, const String &rhs) {
	return String(lhs) + rhs;
}

// `vformat(fmt, args...)` — engine's printf-ish String formatter.
// Engine uses `String::sprintf` with Variant args; we forward to
// snprintf and ignore type-checking. Sufficient for the few log
// strings godot-speech builds with it.
namespace _audio_model {
inline String vformat_impl(const char *fmt) {
	return String(fmt);
}

template <typename T>
inline String vformat_impl(const char *fmt, T arg) {
	char buf[256];
	std::snprintf(buf, sizeof(buf), fmt, arg);
	return String(buf);
}

inline const char *cstr_of(const String &s) {
	return s.utf8().get_data();
}
inline const char *cstr_of(const char *s) {
	return s;
}
} // namespace _audio_model

// vformat with arbitrary args — Godot's signature accepts up to N
// args of any printable type. We forward through snprintf.
inline String vformat(const String &fmt) {
	return fmt;
}

template <typename A>
inline String vformat(const String &fmt, const A &a) {
	(void)a;
	// Replace each `%s` with the next arg via simple substitution.
	// godot-speech uses %s with already-stringified arguments, so a
	// single-pass single-arg replace is sufficient.
	const std::string &src = fmt.std_str();
	auto pos = src.find("%s");
	if (pos == std::string::npos) {
		return fmt;
	}
	String result(src.substr(0, pos));
	if constexpr (std::is_same_v<A, String>) {
		result = result + a;
	} else if constexpr (std::is_same_v<A, const char *>) {
		result = result + String(a);
	} else {
		result = result + itos(static_cast<long long>(a));
	}
	return result + String(src.substr(pos + 2));
}

template <typename A, typename B>
inline String vformat(const String &fmt, const A &a, const B &b) {
	return vformat(vformat(fmt, a), b);
}

// `SNAME("foo")` — the engine's compile-time StringName literal.
// Inlines to a temporary StringName here; matches the value-level
// behavior without the engine's static-cache optimization.
#define SNAME(m_str) StringName(m_str)

class StringName {
	std::string s;

public:
	StringName() = default;
	StringName(const char *p_s) :
			s(p_s ? p_s : "") {}
	StringName(const String &p_s) :
			s(p_s.std_str()) {}

	const String operator*() const { return String(s.c_str()); }
	operator String() const { return String(s.c_str()); }

	bool operator==(const StringName &o) const { return s == o.s; }
	bool operator!=(const StringName &o) const { return s != o.s; }
	bool operator<(const StringName &o) const { return s < o.s; }
};

class NodePath {
	String path;

public:
	NodePath() = default;
	NodePath(const char *p_s) :
			path(p_s) {}
	NodePath(const String &p_s) :
			path(p_s) {}

	const String &get_path() const { return path; }
};

class PackedStringArray {
	std::vector<String> v;

public:
	PackedStringArray() = default;

	void push_back(const String &s) { v.push_back(s); }
	void append(const String &s) { v.push_back(s); }
	int size() const { return static_cast<int>(v.size()); }
	const String &operator[](int i) const { return v[i]; }
	String &operator[](int i) { return v[i]; }
};
