// CHI-101 Phase A pass-3 — `RefCounted` + `Ref<T>` stand-ins.
//
// Same idea as core/object/ref_counted.h: an intrusive ref-count on
// the pointee, and a `Ref<T>` smart pointer that increments/
// decrements on construction/copy/destruction. The engine's full
// `Object` hierarchy isn't reproduced — `RefCounted` here is a
// standalone base class with no signal/method/property machinery.

#pragma once

#include "typedefs.h"

#include <atomic>

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
	T *ptr = nullptr;

public:
	Ref() = default;
	Ref(T *p) :
			ptr(p) {
		if (ptr)
			ptr->reference();
	}
	Ref(const Ref &o) :
			ptr(o.ptr) {
		if (ptr)
			ptr->reference();
	}

	template <typename U>
	Ref(const Ref<U> &o) :
			ptr(static_cast<T *>(o.ptr_raw())) {
		if (ptr)
			ptr->reference();
	}

	~Ref() {
		if (ptr && ptr->unreference()) {
			delete ptr;
		}
	}

	Ref &operator=(const Ref &o) {
		if (this == &o)
			return *this;
		if (o.ptr)
			o.ptr->reference();
		if (ptr && ptr->unreference())
			delete ptr;
		ptr = o.ptr;
		return *this;
	}

	Ref &operator=(T *p) {
		if (p)
			p->reference();
		if (ptr && ptr->unreference())
			delete ptr;
		ptr = p;
		return *this;
	}

	T *operator->() const { return ptr; }
	T &operator*() const { return *ptr; }
	T *ptr_raw() const { return ptr; }

	bool is_valid() const { return ptr != nullptr; }
	bool is_null() const { return ptr == nullptr; }
	void unref() {
		if (ptr && ptr->unreference())
			delete ptr;
		ptr = nullptr;
	}

	bool operator==(const Ref &o) const { return ptr == o.ptr; }
	bool operator!=(const Ref &o) const { return ptr != o.ptr; }
};
