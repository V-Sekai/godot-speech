// CHI-101 Phase A pass-3 — minimal stand-ins for Godot's ERR_FAIL_*
// macros. The reference implementation in core/error/error_macros.h
// integrates with the engine's error logger; here we just print to
// stderr and obey the return-value contract. Sufficient for the
// test binary's needs.

#pragma once

#include "error_list.h"
#include "string.h"
#include "typedefs.h"

#include <cstdio>

#define _STR_(x) #x
#define _STRINGIFY_(x) _STR_(x)
#define _MODEL_LOC_ __FILE__ ":" _STRINGIFY_(__LINE__)

// Helper indirection so callers can pass either `const char*` or
// the model's `String` (which fprintf with %s would otherwise treat
// as a non-POD varargs target).
namespace _audio_model {
_FORCE_INLINE_ const char *as_c_str(const char *s) {
	return s;
}
_FORCE_INLINE_ CharString as_c_str(const String &s) {
	return s.utf8();
}

template <typename T>
_FORCE_INLINE_ const char *cstr(const T &v) {
	auto cs = as_c_str(v);
	return cs.get_data();
}

_FORCE_INLINE_ const char *cstr(const char *s) {
	return s ? s : "";
}
} // namespace _audio_model

#define ERR_PRINT(m_msg)                                               \
	do {                                                               \
		std::fprintf(stderr, "[audio_model ERR " _MODEL_LOC_ "] %s\n", \
				_audio_model::cstr(m_msg));                            \
	} while (0)

#define ERR_PRINT_ONCE(m_msg)                          \
	do {                                               \
		static bool _model_printed_##__LINE__ = false; \
		if (!_model_printed_##__LINE__) {              \
			_model_printed_##__LINE__ = true;          \
			ERR_PRINT(m_msg);                          \
		}                                              \
	} while (0)

#define WARN_PRINT(m_msg)                                               \
	do {                                                                \
		std::fprintf(stderr, "[audio_model WARN " _MODEL_LOC_ "] %s\n", \
				_audio_model::cstr(m_msg));                             \
	} while (0)

#define print_verbose(m_msg) ((void)0)

#define ERR_FAIL_COND(m_cond)                     \
	do {                                          \
		if ((m_cond)) {                           \
			ERR_PRINT("ERR_FAIL_COND: " #m_cond); \
			return;                               \
		}                                         \
	} while (0)

#define ERR_FAIL_COND_V(m_cond, m_retval)           \
	do {                                            \
		if ((m_cond)) {                             \
			ERR_PRINT("ERR_FAIL_COND_V: " #m_cond); \
			return (m_retval);                      \
		}                                           \
	} while (0)

#define ERR_FAIL_COND_MSG(m_cond, m_msg) \
	do {                                 \
		if ((m_cond)) {                  \
			ERR_PRINT((m_msg));          \
			return;                      \
		}                                \
	} while (0)

#define ERR_FAIL_COND_V_MSG(m_cond, m_retval, m_msg) \
	do {                                             \
		if ((m_cond)) {                              \
			ERR_PRINT((m_msg));                      \
			return (m_retval);                       \
		}                                            \
	} while (0)

#define ERR_FAIL_V(m_retval)     \
	do {                         \
		ERR_PRINT("ERR_FAIL_V"); \
		return (m_retval);       \
	} while (0)

#define ERR_FAIL_V_MSG(m_retval, m_msg) \
	do {                                \
		ERR_PRINT((m_msg));             \
		return (m_retval);              \
	} while (0)

#define ERR_FAIL()             \
	do {                       \
		ERR_PRINT("ERR_FAIL"); \
		return;                \
	} while (0)

#define ERR_FAIL_NULL(m_param)                     \
	do {                                           \
		if ((m_param) == nullptr) {                \
			ERR_PRINT("ERR_FAIL_NULL: " #m_param); \
			return;                                \
		}                                          \
	} while (0)

#define ERR_FAIL_NULL_V(m_param, m_retval)           \
	do {                                             \
		if ((m_param) == nullptr) {                  \
			ERR_PRINT("ERR_FAIL_NULL_V: " #m_param); \
			return (m_retval);                       \
		}                                            \
	} while (0)

#define ERR_FAIL_NULL_V_MSG(m_param, m_retval, m_msg) \
	do {                                              \
		if ((m_param) == nullptr) {                   \
			ERR_PRINT(m_msg);                         \
			return (m_retval);                        \
		}                                             \
	} while (0)

#define ERR_FAIL_NULL_MSG(m_param, m_msg) \
	do {                                  \
		if ((m_param) == nullptr) {       \
			ERR_PRINT(m_msg);             \
			return;                       \
		}                                 \
	} while (0)

#define ERR_FAIL_INDEX(m_index, m_size)               \
	do {                                              \
		if ((m_index) < 0 || (m_index) >= (m_size)) { \
			ERR_PRINT("ERR_FAIL_INDEX: " #m_index);   \
			return;                                   \
		}                                             \
	} while (0)

#define ERR_FAIL_INDEX_V(m_index, m_size, m_retval)   \
	do {                                              \
		if ((m_index) < 0 || (m_index) >= (m_size)) { \
			ERR_PRINT("ERR_FAIL_INDEX_V: " #m_index); \
			return (m_retval);                        \
		}                                             \
	} while (0)

#define CRASH_COND(m_cond)                                                             \
	do {                                                                               \
		if ((m_cond)) {                                                                \
			std::fprintf(stderr, "[audio_model CRASH " _MODEL_LOC_ "] " #m_cond "\n"); \
			std::abort();                                                              \
		}                                                                              \
	} while (0)

#define DEV_ASSERT(m_cond) CRASH_COND(!(m_cond))

#define print_line(m_msg)                          \
	do {                                           \
		std::fprintf(stderr, "[audio_model] %s\n", \
				_audio_model::cstr(m_msg));        \
	} while (0)
