// CHI-101 Phase A pass-3 — Godot allocator macros.
// The reference engine routes through a custom Memory::alloc tracker;
// for the test binary we forward straight to libc.

#pragma once

#include "typedefs.h"

#include <cstdlib>
#include <new>

#define memalloc(m_size) (::malloc(m_size))
#define memrealloc(m_ptr, m_size) (::realloc((m_ptr), (m_size)))
#define memfree(m_ptr) (::free(m_ptr))

#define memnew(m_class) (new m_class)
#define memdelete(m_v) (delete (m_v))
