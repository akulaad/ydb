#include <ydb/services/udf_store/blob_chunks.h>
#include <ydb/services/udf_store/wasm/manifest.h>

#include <library/cpp/digest/md5/md5.h>
#include <library/cpp/testing/unittest/registar.h>

using namespace NKikimr::NUdfStore;

Y_UNIT_TEST_SUITE(TUdfStoreUploadApiTest) {

Y_UNIT_TEST(ChunkSplitUnderDatashardLimit) {
    const TString data(WasmBlobChunkSize + 100, 'x');
    const auto chunks = SplitBlob(data);
    UNIT_ASSERT_VALUES_EQUAL(chunks.size(), 2u);
    UNIT_ASSERT(chunks[0].size() <= WasmBlobChunkSize);
    UNIT_ASSERT_VALUES_EQUAL(JoinBlobs(chunks), data);
}

Y_UNIT_TEST(ExpectedMd5MatchesContent) {
    const TString content = "hello-udf-body";
    const TString md5 = MD5::Calc(content);
    UNIT_ASSERT_VALUES_EQUAL(md5.size(), 32u);
    UNIT_ASSERT(md5 != MD5::Calc(content + "!"));
}

Y_UNIT_TEST(BadManifestRejectedByParser) {
    UNIT_ASSERT_EXCEPTION(NWasm::ParseManifest("{not-json"), yexception);
}

Y_UNIT_TEST(WasmManifestRequiredForUploadValidation) {
    const TString manifest = R"({
      "module_name": "LocalUdf",
      "calling_convention": "unversioned_value",
      "module_extension": "wat",
      "required_libraries": [],
      "functions": [
        {
          "name": "udf_add",
          "argument_types": [
            {"value": "int64", "tag": "concrete_type"},
            {"value": "int64", "tag": "concrete_type"}
          ],
          "result_type": {"value": "int64", "tag": "concrete_type"}
        }
      ]
    })";
    const auto parsed = NWasm::ParseManifest(manifest);
    UNIT_ASSERT_VALUES_EQUAL(parsed.ModuleName, "LocalUdf");
}

}
