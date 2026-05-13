// CHI-101 Phase A pass-3 — umbrella include for the core types
// stand-ins. The Godot reference engine spreads these across
// `core/string/`, `core/math/`, `core/templates/`, `core/variant/`;
// for the test binary we keep them flat.

#pragma once

#include "error_list.h"
#include "error_macros.h"
#include "math_funcs.h"
#include "memory.h"
#include "mutex.h"
#include "packed_arrays.h"
#include "ref_counted.h"
#include "string.h"
#include "typedefs.h"
#include "variant.h"
#include "vector.h"
#include "vector2.h"

// Dictionary + Array depend on Variant, so include after.
#include "dictionary.h"
