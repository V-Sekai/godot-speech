// CHI-101 Phase A pass-3 — `Mutex` and `SafeNumeric<T>` stand-ins.
// std::mutex / std::atomic-backed; no recursive variant.

#pragma once

#include "typedefs.h"

#include <atomic>
#include <mutex>

class Mutex {
	std::mutex m;

public:
	void lock() const { const_cast<Mutex *>(this)->m.lock(); }
	void unlock() const { const_cast<Mutex *>(this)->m.unlock(); }
	bool try_lock() const { return const_cast<Mutex *>(this)->m.try_lock(); }
};

class MutexLock {
	const Mutex &m;

public:
	explicit MutexLock(const Mutex &p_m) :
			m(p_m) { m.lock(); }
	~MutexLock() { m.unlock(); }

	MutexLock(const MutexLock &) = delete;
	MutexLock &operator=(const MutexLock &) = delete;
};

template <typename T>
class SafeNumeric {
	std::atomic<T> v{ T(0) };

public:
	SafeNumeric() = default;
	explicit SafeNumeric(T init) :
			v(init) {}

	T get() const { return v.load(std::memory_order_acquire); }
	void set(T x) { v.store(x, std::memory_order_release); }
	T increment() { return v.fetch_add(T(1), std::memory_order_acq_rel) + T(1); }
	T decrement() { return v.fetch_sub(T(1), std::memory_order_acq_rel) - T(1); }
	T add(T x) { return v.fetch_add(x, std::memory_order_acq_rel) + x; }
	T sub(T x) { return v.fetch_sub(x, std::memory_order_acq_rel) - x; }
};
