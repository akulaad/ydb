#include <ydb/services/udf_store/wasm/abi/udf_cpp_abi.h>

#include <util/generic/yexception.h>

using namespace NYdb::NUdfStore::NAbi;

//! Guest C++ exception (yexception), not the host ThrowException import.
//! Nested helpers keep stable wasm names for readable call stacks.
extern "C" {

__attribute__((visibility("default"))) void boom_leaf() {
    ythrow yexception() << "boom-yexception-from-wasm";
}

__attribute__((visibility("default"))) void boom_middle() {
    boom_leaf();
}

__attribute__((visibility("default"))) void fail(
    TExpressionContext* /*context*/,
    TUnversionedValue* /*result*/)
{
    boom_middle();
}

} // extern "C"
