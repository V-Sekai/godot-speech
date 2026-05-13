// The engine's `core/object/ref_counted.h` transitively pulls in all
// the core types (Variant, PackedByteArray, GDCLASS, etc.) via
// `core/object/object.h`. Reproduce that bundle here so engine
// .cpp files only #including ref_counted.h see the full surface.
#pragma once

#include "../../../core/core_types.h"
#include "class_db.h"
