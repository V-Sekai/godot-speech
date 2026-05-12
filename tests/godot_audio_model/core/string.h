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

// `itos` mirrors Godot's int-to-String helper.
inline String itos(long long v) {
	return String(std::to_string(v));
}
inline String itos(int v) {
	return String(std::to_string(v));
}
inline String itos(unsigned int v) {
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
