// Anchor translation unit that forces the linker to retain backend
// registration code when building libatlas.so / libatlas.a.
//
// The symbols referenced here are defined in alwayslink backend targets.
// Without this anchor, `ld -shared` may strip the static initializers
// that call BackendFactory::Register().

#include "src/backend/base/backend_factory.h"
#include "src/backend/cpu/cpu_backend.h"
#include "src/backend/cpu/cpu_backend_context.h"

namespace atlas {
namespace public_api {

// Returns the list of backends compiled into this library build.
// This function is called to log available backends, and its mere
// existence forces the linker to pull in the cpu_backend /
// cpu_backend_context translation units.
void EnsureBackendsLinked() {
    auto& factory = ::atlas::backend::BackendFactory::Instance();
    (void)factory.ListBackends();
}

}  // namespace public_api
}  // namespace atlas
