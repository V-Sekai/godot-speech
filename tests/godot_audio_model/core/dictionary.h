// CHI-101 Phase A pass-7 — minimal `Dictionary` + `Array` stand-ins.
//
// The reference engine's Dictionary is a hash-ordered Variant-to-Variant
// map; Array is a Variant vector. godot-speech uses both as plain
// containers for packet metadata (`packet`, `valid`, `sequence_id`,
// `excess_packets`, `audio_stream_player`, `playback_stats`, etc.).
// We back them with std::map<String, Variant> and std::vector<Variant>
// — same observable surface for the test path, no engine RTTI.

#pragma once

#include "string.h"
#include "typedefs.h"
#include "variant.h"

#include <map>
#include <vector>

class Dictionary {
	std::map<String, Variant> m;

public:
	Dictionary() = default;

	bool has(const String &key) const { return m.find(key) != m.end(); }
	bool has(const char *key) const { return has(String(key)); }
	bool has(const StringName &key) const { return has(static_cast<String>(key)); }
	// Engine accepts any Variant as a key — we normalize via
	// `static_cast<String>(v)` which uses Variant's string slot
	// (set by Variant(int) → empty s + INT path needs adjustment;
	// for the speech code path the int-keyed dict gets stringified
	// via itos at the call site, but the engine's Dictionary
	// silently stringifies on lookup — emulate via itos).
	bool has(int key) const { return has(itos(key).std_str()); }
	bool has(int64_t key) const { return has(itos(key).std_str()); }
	bool has(const Variant &key) const {
		if (key.get_type() == Variant::STRING) {
			return has(static_cast<String>(key));
		}
		return has(itos(static_cast<int64_t>(key)).std_str());
	}

	Variant &operator[](const String &key) { return m[key]; }
	Variant &operator[](const char *key) { return m[String(key)]; }
	Variant &operator[](int key) { return m[itos(key)]; }
	Variant &operator[](int64_t key) { return m[itos(key)]; }
	Variant &operator[](const Variant &key) {
		if (key.get_type() == Variant::STRING) {
			return m[static_cast<String>(key)];
		}
		return m[itos(static_cast<int64_t>(key))];
	}
	const Variant operator[](const String &key) const {
		auto it = m.find(key);
		return it == m.end() ? Variant() : it->second;
	}
	const Variant operator[](const char *key) const { return (*this)[String(key)]; }
	const Variant operator[](int key) const { return (*this)[itos(key)]; }
	const Variant operator[](int64_t key) const { return (*this)[itos(key)]; }
	const Variant operator[](const Variant &key) const {
		if (key.get_type() == Variant::STRING) {
			return (*this)[static_cast<String>(key)];
		}
		return (*this)[itos(static_cast<int64_t>(key))];
	}

	int size() const { return static_cast<int>(m.size()); }
	bool is_empty() const { return m.empty(); }
	void clear() { m.clear(); }

	bool erase(const String &key) { return m.erase(key) > 0; }
	bool erase(int key) { return m.erase(itos(key)) > 0; }

	class Iter {
		typename std::map<String, Variant>::iterator it;
		typename std::map<String, Variant>::iterator end;

	public:
		Iter(typename std::map<String, Variant>::iterator b,
				typename std::map<String, Variant>::iterator e) :
				it(b), end(e) {}
		bool valid() const { return it != end; }
		const String &key() const { return it->first; }
		Variant &value() { return it->second; }
		void advance() { ++it; }
	};
	Iter iter() { return Iter(m.begin(), m.end()); }

	Dictionary duplicate(bool /*deep*/ = false) const {
		Dictionary d;
		d.m = m;
		return d;
	}

	// Forward-declare in-class; defined after Array below via
	// the free helper.
	class Array keys() const;
};

class Array {
	std::vector<Variant> v;

public:
	Array() = default;

	int size() const { return static_cast<int>(v.size()); }
	bool is_empty() const { return v.empty(); }
	void clear() { v.clear(); }
	void resize(int n) { v.resize(static_cast<size_t>(n)); }

	Variant &operator[](int i) { return v[i]; }
	const Variant &operator[](int i) const { return v[i]; }

	void push_back(const Variant &x) { v.push_back(x); }
	void append(const Variant &x) { v.push_back(x); }
	Variant pop_front() {
		if (v.empty()) {
			return Variant();
		}
		Variant front = v.front();
		v.erase(v.begin());
		return front;
	}
	Variant back() const { return v.empty() ? Variant() : v.back(); }
	Variant front() const { return v.empty() ? Variant() : v.front(); }

	// Engine API: keys() on a Dictionary returns an Array.
	static Array from_dictionary_keys(const Dictionary &d) {
		Array a;
		Dictionary nonconst = d;
		auto it = nonconst.iter();
		while (it.valid()) {
			a.push_back(Variant(it.key()));
			it.advance();
		}
		return a;
	}
};

// Member-style `Dictionary::keys()` — forward-declared inside the
// class isn't ideal since Array depends on Dictionary; define it
// here as a free helper plus a method body via injected friend.
inline Array dictionary_keys_method(Dictionary &d) {
	return Array::from_dictionary_keys(d);
}

// Engine: `dict.keys()` is a member. We can't add to Dictionary
// after the fact in C++, so callers using `dict.keys()` need a
// method. Add it via header-level macro injection isn't clean —
// instead, augment Dictionary inline. Since this header defines
// Dictionary already, append a free-friend-style helper.

// Variant carriers for Dictionary / Array — defined here once the
// full types are visible.
inline Variant::Variant(const Dictionary &v) :
		type(DICTIONARY),
		dict(std::make_shared<Dictionary>(v)) {}
inline Variant::Variant(const Array &v) :
		type(ARRAY),
		arr(std::make_shared<Array>(v)) {}
inline Variant &Variant::operator=(const Dictionary &v) {
	*this = Variant(v);
	return *this;
}
inline Variant &Variant::operator=(const Array &v) {
	*this = Variant(v);
	return *this;
}
inline Variant::operator Dictionary() const {
	return dict ? *dict : Dictionary();
}
inline Variant::operator Array() const {
	return arr ? *arr : Array();
}

// Engine's Dictionary::keys() — defined here to avoid Dictionary
// having to know about Array.
inline Array dictionary_keys(const Dictionary &d) {
	return Array::from_dictionary_keys(d);
}

// Member `Dictionary::keys()` body. Inline to keep this a
// header-only model component.
inline Array Dictionary::keys() const {
	return Array::from_dictionary_keys(*this);
}
