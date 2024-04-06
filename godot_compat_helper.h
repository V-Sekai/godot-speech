#pragma once

#include <godot_compat/core/error_macros.hpp>
#include <godot_compat/variant/variant.hpp>

#ifndef GODOT_MODULE_COMPAT
#include <godot_cpp/variant/utility_functions.hpp>
#endif

using namespace godot;

inline void compat_print_line(const Variant &v) {
#ifdef GODOT_MODULE_COMPAT
	print_line(v);
#else
	UtilityFunctions::print(v);
#endif
}

inline void compat_print_error(const Variant &v) {
#ifdef GODOT_MODULE_COMPAT
	print_error(v);
#else
	ERR_PRINT(v);
#endif
}
