#include <cstdint>

namespace WAVM {
namespace Runtime {
struct ContextRuntimeData;
} // namespace Runtime
} // namespace WAVM

extern "C" __attribute__((visibility("default"))) int64_t native_add(
    WAVM::Runtime::ContextRuntimeData* /*ctx*/,
    int64_t a,
    int64_t b)
{
    return a + b;
}
