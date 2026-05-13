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

	Variant &operator[](const String &key) { return m[key]; }
	Variant &operator[](const char *key) { return m[String(key)]; }
	const Variant operator[](const String &key) const {
		auto it = m.find(key);
		return it == m.end() ? Variant() : it->second;
	}
	const Variant operator[](const char *key) const { return (*this)[String(key)]; }

	int size() const { return static_cast<int>(m.size()); }
	bool is_empty() const { return m.empty(); }
	void clear() { m.clear(); }

	bool erase(const String &key) { return m.erase(key) > 0; }

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
};

class Array {
	std::vector<Variant> v;

public:
	Array() = default;

	int size() const { return static_cast<int>(v.size()); }
	bool is_empty() const { return v.empty(); }
	void clear() { v.clear(); }

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

// Engine's Dictionary::keys() — defined here to avoid Dictionary
// having to know about Array.
inline Array dictionary_keys(const Dictionary &d) {
	return Array::from_dictionary_keys(d);
}
