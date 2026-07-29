#include <ydb/services/udf_store/wasm/manifest.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace NKikimr::NUdfStore::NWasm;

Y_UNIT_TEST_SUITE(TWasmManifestTest) {

Y_UNIT_TEST(ParseValidManifest) {
    const TString manifest = R"({
        "module_name": "LocalUdf",
        "calling_convention": "unversioned_value",
        "functions": [
            {
                "name": "udf_add",
                "argument_types": [
                    {"value": "int64", "tag": "concrete_type"},
                    {"value": "int64", "tag": "concrete_type"}
                ],
                "result_type": {"value": "int64", "tag": "concrete_type"}
            }
        ],
        "required_libraries": []
    })";

    const auto parsed = ParseManifest(manifest);
    UNIT_ASSERT_VALUES_EQUAL(parsed.ModuleName, "LocalUdf");
    UNIT_ASSERT_VALUES_EQUAL(parsed.CallingConvention, "unversioned_value");
    UNIT_ASSERT(parsed.RequiredLibraries.empty());
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions.size(), 1u);
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[0].Name, "udf_add");
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[0].Args.size(), 2u);
    UNIT_ASSERT(parsed.Functions[0].Result == EUdfValueType::Int64);
}

Y_UNIT_TEST(ParseRequiredLibraries) {
    const TString manifest = R"({
        "module_name": "LocalUdf",
        "functions": [
            {
                "name": "udf_add",
                "argument_types": [],
                "result_type": {"value": "int64", "tag": "concrete_type"}
            }
        ],
        "required_libraries": ["helpers-lib", "helpers"]
    })";

    const auto parsed = ParseManifest(manifest);
    UNIT_ASSERT_VALUES_EQUAL(parsed.RequiredLibraries.size(), 2u);
    UNIT_ASSERT_VALUES_EQUAL(parsed.RequiredLibraries[0], "helpers-lib");
    UNIT_ASSERT_VALUES_EQUAL(parsed.RequiredLibraries[1], "helpers");
}

Y_UNIT_TEST(ParseObjectsTypeConfigCallable) {
    const TString manifest = R"({
        "module_name": "Prefix",
        "calling_convention": "unversioned_value",
        "required_libraries": ["sdk"],
        "objects": [
            {
                "name": "Prefix",
                "create_export": "prefix_create",
                "destroy_export": "prefix_destroy",
                "methods": [
                    {
                        "name": "Apply",
                        "export": "prefix_apply",
                        "yql_binding": "type_config_callable",
                        "argument_types": [
                            {"value": "string", "tag": "concrete_type"}
                        ],
                        "result_type": {"value": "string", "tag": "concrete_type"}
                    }
                ]
            }
        ]
    })";

    const auto parsed = ParseManifest(manifest);
    UNIT_ASSERT_VALUES_EQUAL(parsed.Objects.size(), 1u);
    // New (from create_export) + Apply
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions.size(), 2u);
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[0].Name, "New");
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[0].ExportName, "prefix_create");
    UNIT_ASSERT(parsed.Functions[0].Binding == EWasmUdfBinding::Plain);
    UNIT_ASSERT(parsed.Functions[0].Result == EUdfValueType::Uint64);
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[1].Name, "Apply");
    UNIT_ASSERT(parsed.Functions[1].Binding == EWasmUdfBinding::TypeConfigCallable);
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[1].CreateExport, "prefix_create");
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[1].CallExport, "prefix_apply");
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[1].DestroyExport, "prefix_destroy");
}

Y_UNIT_TEST(ParseObjectsPlainSnapshot) {
    const TString manifest = R"({
        "module_name": "Ctx",
        "objects": [
            {
                "name": "Ctx",
                "create_export": "ctx_create",
                "destroy_export": "ctx_destroy",
                "methods": [
                    {
                        "name": "Snapshot",
                        "export": "ctx_snapshot",
                        "yql_binding": "plain",
                        "argument_types": [
                            {"value": "uint64", "tag": "concrete_type"}
                        ],
                        "result_type": {"value": "string", "tag": "concrete_type"}
                    }
                ]
            }
        ]
    })";

    const auto parsed = ParseManifest(manifest);
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions.size(), 2u);
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[0].Name, "New");
    UNIT_ASSERT_VALUES_EQUAL(TString(PlainWasmExport(parsed.Functions[0])), "ctx_create");
    UNIT_ASSERT_VALUES_EQUAL(parsed.Functions[1].Name, "Snapshot");
    UNIT_ASSERT(parsed.Functions[1].Binding == EWasmUdfBinding::Plain);
    UNIT_ASSERT_VALUES_EQUAL(TString(PlainWasmExport(parsed.Functions[1])), "ctx_snapshot");
}

Y_UNIT_TEST(RejectTypeConfigOnPlainFunctions) {
    const TString manifest = R"({
        "module_name": "Bad",
        "functions": [
            {
                "name": "x",
                "yql_binding": "type_config_callable",
                "argument_types": [],
                "result_type": {"value": "int64", "tag": "concrete_type"}
            }
        ]
    })";
    UNIT_ASSERT_EXCEPTION(ParseManifest(manifest), yexception);
}

Y_UNIT_TEST(RejectEmptyManifest) {
    UNIT_ASSERT_EXCEPTION(ParseManifest(""), yexception);
}

Y_UNIT_TEST(ParseRequiredNativeModules) {
    const TString manifest = R"({
        "module_name": "WithNativeHost",
        "functions": [
            {
                "name": "udf_add",
                "argument_types": [],
                "result_type": {"value": "int64", "tag": "concrete_type"}
            }
        ],
        "required_native_modules": ["native_math"]
    })";

    const auto parsed = ParseManifest(manifest);
    UNIT_ASSERT_VALUES_EQUAL(parsed.RequiredNativeModules.size(), 1u);
    UNIT_ASSERT_VALUES_EQUAL(parsed.RequiredNativeModules[0], "native_math");
}

Y_UNIT_TEST(ParseNativeHostManifest) {
    const TString manifest = R"({
        "module_name": "native_math",
        "host_exports": [
            {
                "name": "host_add",
                "symbol": "native_add",
                "params": ["i64", "i64"],
                "results": ["i64"]
            }
        ]
    })";

    const auto parsed = ParseNativeHostManifest(manifest);
    UNIT_ASSERT_VALUES_EQUAL(parsed.ModuleName, "native_math");
    UNIT_ASSERT_VALUES_EQUAL(parsed.HostExports.size(), 1u);
    UNIT_ASSERT_VALUES_EQUAL(parsed.HostExports[0].Name, "host_add");
    UNIT_ASSERT_VALUES_EQUAL(parsed.HostExports[0].Symbol, "native_add");
    UNIT_ASSERT_VALUES_EQUAL(parsed.HostExports[0].Params.size(), 2u);
    UNIT_ASSERT(parsed.HostExports[0].Params[0] == EWasmHostValueType::I64);
    UNIT_ASSERT_VALUES_EQUAL(parsed.HostExports[0].Results.size(), 1u);
    UNIT_ASSERT(HasHostExports(manifest));
}

Y_UNIT_TEST(RejectNativeHostWithoutModuleName) {
    const TString manifest = R"({
        "host_exports": [
            {"name": "host_add", "params": ["i32"], "results": ["i32"]}
        ]
    })";
    UNIT_ASSERT_EXCEPTION(ParseNativeHostManifest(manifest), yexception);
}

} // Y_UNIT_TEST_SUITE
