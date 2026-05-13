// CHI-101 Phase A pass-7 — engine-path shim for the GDCLASS /
// ClassDB binding machinery.
//
// The reference engine registers every class + method + signal +
// property with `core/object/class_db.h` so GDScript can reflect
// on them. godot-speech uses the binding API in `_bind_methods()`
// but the test binary never exercises it (no script engine, no
// editor, no save/load). We define the macros as no-ops and provide
// a stub `ClassDB::bind_method` that swallows its arguments at
// compile time.
#pragma once

#include "../../../core/core_types.h"

// Marker macro: a class is "registered" with the engine.  The engine
// generates a `static ClassDB` registration; we emit nothing.
#define GDCLASS(m_class, m_inherits) \
public:                              \
	using BaseClass = m_inherits;    \
                                     \
private:

// Bound to `ClassDB::bind_method(D_METHOD("name", ...), &Class::method)`.
// In the test binary the binding is a no-op — direct C++ calls reach
// the same code path.
#define D_METHOD(...) ::godot_model::MethodDescriptor()

#define ADD_SIGNAL(m_sig) ((void)(m_sig))
#define ADD_PROPERTY(m_prop, m_setter, m_getter) \
	((void)(m_prop));                            \
	((void)(m_setter));                          \
	((void)(m_getter))
#define ADD_PROPERTY_DEFAULT(m_prop, m_default) \
	((void)(m_prop));                           \
	((void)(m_default))
#define BIND_CONSTANT(m_const) ((void)(m_const))
#define BIND_ENUM_CONSTANT(m_const) ((void)(m_const))

#define PROPERTY_HINT_NONE 0
#define PROPERTY_USAGE_NONE 0

namespace godot_model {
struct MethodDescriptor {
	template <typename... Args>
	MethodDescriptor(Args &&...) {}
};

struct PropertyInfoStub {
	template <typename... Args>
	PropertyInfoStub(Args &&...) {}
};

struct MethodInfoStub {
	template <typename... Args>
	MethodInfoStub(Args &&...) {}
};
} // namespace godot_model

using PropertyInfo = ::godot_model::PropertyInfoStub;
using MethodInfo = ::godot_model::MethodInfoStub;

// `Variant::DICTIONARY`, `Variant::INT`, etc. — used as second
// argument to PropertyInfo. Variant has a Type enum; expose the
// engine's spelling.
namespace VariantEngineCompat {
inline constexpr int DICTIONARY = ::Variant::OBJECT; // close enough; PropertyInfo doesn't read this
inline constexpr int INT = ::Variant::INT;
inline constexpr int FLOAT = ::Variant::FLOAT;
inline constexpr int BOOL = ::Variant::BOOL;
inline constexpr int STRING = ::Variant::STRING;
inline constexpr int PACKED_VECTOR2_ARRAY = ::Variant::OBJECT;
} // namespace VariantEngineCompat

// ClassDB stub: every bind_method call is dropped on the floor.
class ClassDB {
public:
	template <typename... Args>
	static void bind_method(Args &&...) {}
	template <typename... Args>
	static void add_signal(Args &&...) {}
	template <typename... Args>
	static void add_property(Args &&...) {}
};
