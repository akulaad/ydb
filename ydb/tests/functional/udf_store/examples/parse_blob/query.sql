/* syntax version 1 */
-- Host→Guest zero-copy demo for heavy blob columns.
--
-- On a stage with WASM UDFs, string/blob cells larger than the embedded
-- UnboxedValue buffer are written once into the query compartment's linear
-- memory at scan materialization. ParseBlob::parse_blob then receives only
-- wasm_offset + length (no second CopyIntoCompartment memcpy).
--
-- Build (wasm64):
--   ya make --target-platform=clang20-emscripten-wasm64 --build profile \
--     ydb/tests/functional/udf_store/examples/parse_blob
--
-- Upload order: library sdk → UDF parse_blob (see manifest required_libraries).

-- Literal path (still uses the same ABI; large literals also land in WASM).
SELECT ParseBlob::parse_blob(
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
) AS from_literal;

-- Table/column path matching: SELECT ParseBlob::parse_blob(blob) FROM data
SELECT
    ParseBlob::parse_blob(blob) AS parsed
FROM AS_TABLE([
    <|blob: "hello world, this payload is longer than fourteen bytes"|>,
    <|blob: "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"|>,
    <|blob: NULL|>
]);
