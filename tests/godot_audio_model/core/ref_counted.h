// CHI-101 Phase A — `RefCounted` + `Ref<T>` stand-ins.
//
// Same idea as core/object/ref_counted.h: an intrusive ref-count on
// the pointee, and a `Ref<T>` smart pointer that increments/
// decrements on construction/copy/destruction. The engine's full
// `Object` hierarchy isn't reproduced — `RefCounted` here is a
// standalone base class with no signal/method/property machinery.
//
// GDCLASS lives here too (engine puts it in core/object/object.h
// which is transitively pulled in by ref_counted.h). Keeping the
// macro alongside RefCounted means every header that declares a
// reference-counted class also has the macro visible.

#pragma once

#include "typedefs.h"

#include <atomic>

// GDCLASS marker — the engine's macro generates ClassDB
// registration boilerplate. We emit just a `BaseClass` typedef so
// engine code that uses `BaseClass::method()` from inside the class
// body keeps compiling. Method registration itself is a no-op
// (test binary doesn't reflect into GDScript).
#ifndef GDCLASS
#define GDCLASS(m_class, m_inherits) \
public:                              \
	using BaseClass = m_inherits;    \
                                     \
private:
#endif

class RefCounted {
	mutable std::atomic<int> ref_count{ 0 };

public:
	virtual ~RefCounted() = default;

	void reference() const { ref_count.fetch_add(1, std::memory_order_acq_rel); }
	bool unreference() const {
		return ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1;
	}
	int get_reference_count() const {
		return ref_count.load(std::memory_order_acquire);
	}
};

template <typename T>
class Ref {
	// Storage. The member is named `_ref` to free the `ptr` and
	// `ptr_raw` identifiers for the engine-style accessor methods.
	T *_ref = nullptr;

public:
	Ref() = default;
	Ref(T *p) :
			_ref(p) {
		if (_ref) {
			_ref->reference();
		}
	}
	Ref(const Ref &o) :
			_ref(o._ref) {
		if (_ref) {
			_ref->reference();
		}
	}

	template <typename U>
	Ref(const Ref<U> &o) :
			_ref(static_cast<T *>(o.ptr_raw())) {
		if (_ref) {
			_ref->reference();
		}
	}

	~Ref() {
		if (_ref && _ref->unreference()) {
			delete _ref;
		}
	}

	Ref &operator=(const Ref &o) {
		if (this == &o) {
			return *this;
		}
		if (o._ref) {
			o._ref->reference();
		}
		if (_ref && _ref->unreference()) {
			delete _ref;
		}
		_ref = o._ref;
		return *this;
	}

	Ref &operator=(T *p) {
		if (p) {
			p->reference();
		}
		if (_ref && _ref->unreference()) {
			delete _ref;
		}
		_ref = p;
		return *this;
	}

	T *operator->() const { return _ref; }
	T &operator*() const { return *_ref; }

	// Engine API names: both ptr() and ptr_raw() return the raw
	// pointer. The engine has only ptr(); we keep ptr_raw() too
	// for backward compat with earlier pass code in the model.
	T *ptr() const { return _ref; }
	T *ptr_raw() const { return _ref; }

	// Engine API: Ref<T>::instantiate() creates a new instance.
	void instantiate() {
		if (_ref && _ref->unreference()) {
			delete _ref;
		}
		_ref = new T();
		if (_ref) {
			_ref->reference();
		}
	}

	bool is_valid() const { return _ref != nullptr; }
	bool is_null() const { return _ref == nullptr; }
	void unref() {
		if (_ref && _ref->unreference()) {
			delete _ref;
		}
		_ref = nullptr;
	}

	bool operator==(const Ref &o) const { return _ref == o._ref; }
	bool operator!=(const Ref &o) const { return _ref != o._ref; }
};
