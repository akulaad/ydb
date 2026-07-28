# -*- coding: utf-8 -*-
# Shared constants for the UDF store.
# Must match paths under .metadata/udf_store.

UDF_STORE_PATH = ".metadata/udf_store"
UDF_TABLE_MODULES_PATH = UDF_STORE_PATH + "/modules"
UDF_TABLE_MODULE_CHUNKS_PATH = UDF_STORE_PATH + "/module_chunks"
UDF_KV_BINARIES_PATH = UDF_STORE_PATH + "/binaries"
<<<<<<< HEAD
<<<<<<< HEAD
UDF_ARTIFACTS_DIR_PATH = UDF_STORE_PATH + "/artifacts"

# Backward-compatible aliases used by older test helpers.
UDF_TABLE_META_PATH = UDF_TABLE_MODULES_PATH
=======
UDF_TABLE_WASM_SOURCE_PATH = UDF_STORE_PATH + "/wasm_source"
UDF_TABLE_WASM_SOURCE_CHUNKS_PATH = UDF_STORE_PATH + "/wasm_source_chunks"
UDF_TABLE_LIBRARY_SOURCE_PATH = UDF_STORE_PATH + "/library_source"
UDF_TABLE_LIBRARY_SOURCE_CHUNKS_PATH = UDF_STORE_PATH + "/library_source_chunks"
=======
>>>>>>> 98bfa56d974 (add ddl function)
UDF_ARTIFACTS_DIR_PATH = UDF_STORE_PATH + "/artifacts"
>>>>>>> ceaf113964f (fixes)

# Backward-compatible aliases used by older test helpers.
UDF_TABLE_META_PATH = UDF_TABLE_MODULES_PATH

# Must match NKikimr::NUdfStore::WasmBlobChunkSize
WASM_BLOB_CHUNK_SIZE = 8 * 1024 * 1024
