// Pull in OS too — the engine's scene/main/node.h transitively
// exposes OS::get_singleton through its include chain, and several
// engine .cpp files rely on that.
#pragma once
#include "../../../audio/node.h"
#include "../../core/os/os.h"
