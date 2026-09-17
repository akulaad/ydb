# Local WASM UDF integration worktree

Branch: `test/wasm-udf-integration`.
Worktree: `/home/kulaad/ydbwork/ydb-wasm-integration`.
Local cluster: `/home/kulaad/ydbd`, database `/Root/test`, endpoint
`grpc://localhost:31011`.

This branch is a local integration target for merging and checking fixes before
moving them to their individual development branches. Keep incoming changes in
separate commits or merges so failures can be traced to their source.

## Imported changes

- `YQ-5689_wasm_compile_controller` at `ef26011dc8a`: base.
- `YQ-5689_udf_manifest` at `286e790e3f4`: merged unified module/library
  manifests and standalone fixture tests.
- `YQ-5689_udf_rpc_cli` at `0e6cfa138d5`: merged CLI, SDK, gRPC service and
  tests; its latest worktree changes are stored in `fe604f1f114`.
- `fix/wasm-udf-chunked-reads` at `9ae7c670cc5`: merged paginated source and
  artifact reads, persistent compilation errors and regression tests.
- `wt/reef-profile-wasm` at `55b26f25312`: merged Protobuf and LogParsing examples.
- Uncommitted snapshot from `ydb-reef-profile-wasm`: ReefProfile guest, complete
  proto import dependencies and wasm64 build compatibility changes.
- Uncommitted example changes from `ydb-log-parsing`: LogParsing fixes and
  Yexception example. Generated build artifacts are excluded.

The original worktrees were left unchanged. Snapshot imports are committed only
on this integration branch; upstream branches can subsequently be merged here.

## Fixes found during integration

- Source and artifact reads use pages of four chunks (32 MiB for the normal
  8 MiB chunks), avoiding the 48 MiB query reply limit hit by ReefProfile.
- Query failures in module/library compilation persist `failed`, including
  corrupt or missing source chunks, instead of leaving `compiling` forever.
- Binary export validation parses and validates WASM IR without LLVM codegen.
  Previously `Runtime::loadBinaryModule` compiled the entire module here and
  `CompileModuleObjectCode` compiled it again. A profile of ReefProfile caught
  the first unnecessary pass inside `CollectWasmExports`.
- LLVM 16 releases oversized empty SelectionDAG CSE tables between functions.
  The live compiler retained 1,048,576 buckets with zero nodes; a worker-only
  profile spent 76% of samples in `SelectionDAG::clear` zeroing that table.
- The loader grows the shared function table to the incoming import's minimum
  before linking. ReefProfile requires 31,144 entries; growing only during
  instantiation fails WAVM's earlier import check with a misleading missing
  `__indirect_function_table` error. Both regular and precompiled loading are
  covered, and the compartment's upper bound remains enforced.
- The no-EH libunwind implementation supplies the symbols referenced by the
  exported libc++abi personality functions. Unsupported unwinding traps;
  `_Unwind_DeleteException` performs its normal cleanup callback. These symbols
  are exported by the SDK and are needed when linking real guest modules.
- ReefProfile uses the existing `yt/yt_proto/yt/core` proto library and canonical
  `yt_proto/...` import. A second generator for the same protobuf had produced
  incompatible descriptor names and conflicting generated headers.
- ReefProfile returns expected input errors explicitly because guest C++
  unwinding is disabled. This preserves `null_on_exception` for malformed
  protobuf, codec IDs, zstd payloads and unsupported delta patches. The SQL
  example passes the required nullable options argument explicitly.
- Bridge handle reuse checks kinds and declared type metadata. MiniKQL stores
  `Optional<Dict>` and its dict payload with the same identity; reusing the
  wrapper while unwrapping previously made `BridgeDictLookup` receive Optional.
  Regression tests cover a dict lookup through the actual WASM intrinsics and
  a refcounted string sharing its optional wrapper's identity.
- ReefProfile unwraps explicit optional function arguments before reading their
  payloads. Struct traversal already exposes optional scalar fields as scalars.
- ReefProfile splits codec IDs locally to avoid a wasm-ld relative data import
  of the newer StringSplitter's external sentinel.

## Build and test

Run from this worktree; do not edit compilation inputs while builds are running.

```sh
./ya make --target-platform=clang18-emscripten-wasm64 \
  ydb/udfs/wasm/sdk ydb/udfs/wasm/reef_profile \
  ydb/udfs/wasm/protobuf ydb/udfs/wasm/log_parsing
./ya make --build relwithdebinfo ydb/apps/ydbd ydb/apps/ydb
./ya make --build relwithdebinfo -tA \
  ydb/library/wasm/unittests \
  ydb/services/udf_store/ut_orchestration ydb/tests/functional/udf_store \
  ydb/udfs/wasm/reef_profile/ut \
  -F '*Wasm*' -F '*wasm*' -F '*udf_cli*' -F '*ImportedTable*'
./ya make --build relwithdebinfo -tA ydb/services/udf_store/ut
```

Use Clang 18 for these guests. Clang 20.1.8 emits an invalid wasm64 import
signature for `__builtin_frexpl(long double, int*)`: the pointer becomes `i32`
in the `frexpl` import while the caller passes `i64`. This was reproduced with
a standalone function and confirmed by wasm-tools validation of ReefProfile.
Clang 18.1.8 generates the correct signature.

## Local deployment

Point `/home/kulaad/ydbd/ydbd/bin/ydbd` at this worktree's
`ydb/apps/ydbd/ydbd`, then run `/home/kulaad/ydbd/restart_cluster.sh`.
The local restart script checks the UDF metadata table using `YDB_BIN` and
bounds node shutdown, killing only this cluster's remaining nodes after six
seconds. This prevents an old synchronous LLVM compilation from surviving a
restart in the background.

Both storage and tenant nodes must be restarted. This reuses the existing
cluster data; do not recreate its disk or metadata tables.

Use this worktree's CLI explicitly:

```sh
export YDB_BIN=/home/kulaad/ydbwork/ydb-wasm-integration/ydb/apps/ydb/ydb
upload_script=/home/kulaad/.agents/skills/infra/ydb-udf-cli/scripts/upload_and_wait.sh
"$upload_script" \
  --file ydb/udfs/wasm/sdk/libwasm-sdk.so \
  --manifest ydb/udfs/wasm/sdk/manifest.json --timeout 900
"$upload_script" \
  --file ydb/udfs/wasm/reef_profile/libwasm-reef_profile.so \
  --manifest ydb/udfs/wasm/reef_profile/manifest.json --timeout 1800
"$YDB_BIN" -e grpc://localhost:31011 -d /Root/test \
  udf describe --name ReefProfile --format json
"$YDB_BIN" -e grpc://localhost:31011 -d /Root/test \
  sql --format json-unicode -f ydb/udfs/wasm/reef_profile/query.sql
python3 ydb/udfs/wasm/reef_profile/smoke.py
```

Re-uploading assigns a new UID, which also allows a module whose old UID exhausted
the controller's compilation attempts to compile again.

The smoke script verifies the local artifact's MD5 against the deployed module,
module and platform readiness, and actual SQL results: row reconstruction,
serialized protobuf, empty input, `null_on_exception`, zstd data and the default
error path. ReefProfile returns JSON rather than the native UDF's YQL Struct;
delta patches remain unsupported and are reported as input errors.

## Verified on 2026-09-16

- Host `ydbd` and `ydb` built successfully in `relwithdebinfo`.
- SDK, ReefProfile, Protobuf and LogParsing guests built with Clang 18 wasm64.
- 53 selected orchestration, functional/CLI, loader and ReefProfile tests passed;
  all 151 UDF store unit tests passed (204 tests across these suites).
- Both local nodes were restarted on the final server build.
- SDK UID: `d9362ad5-c69887f4-b64e474c-32b5fa56`, status `ready`.
- ReefProfile UID: `407365fb-2db0fb80-f4ae1dae-9e7d059`.
- ReefProfile MD5: `81b56a413ec8a96181f3b9758567d429`, size: 78,440,135 bytes.
- Module and platform status: `ready`. Upload plus compilation polling and
  the final settle delay took 426.35 seconds (7 minutes 6 seconds).
- `python3 ydb/udfs/wasm/reef_profile/smoke.py` exited successfully after the
  restart and verified every check listed above. The successful row result was
  `{"UserID":"u1","RequestID":"r1","FeedsOutputCache":{"OutputShowBlockIds":["b1"]}}`.

Large-module query setup remains expensive: the two successful sample SQL
requests took about 31 seconds each on this local instance. The smoke script
therefore takes over a minute; `ready` does not imply instant query startup.

Local evidence logs (temporary files):

- `/tmp/wasm-integration-final-server-cli-build.log`
- `/tmp/wasm-integration-table-link-tests.log`
- `/tmp/wasm-integration-store-final-tests.log`
- `/tmp/wasm-integration-reef-final-upload.log`
- `/tmp/wasm-integration-restart-final.log`
- `/tmp/wasm-integration-smoke-success.json`

## Verified after manifest/CLI refresh on 2026-09-17

- `sdk` and `types` built with Clang 18 wasm64 and passed `wasm-tools validate`.
- Host `ydbd` and `ydb` built successfully in `relwithdebinfo`.
- 195 selected manifest, CLI, WASM and chunked-read tests passed.
- Both local cluster nodes were restarted on the refreshed server build.
- SDK UID: `6e5e1502-bbd60029-69622b14-21ae5861`, MD5
  `a90e3f26becc43c545ddb27832fea924`, module and platform status `ready`.
- BridgeTypes UID: `63f4bab3-6aea04bd-29fe5b08-e5cf6a02`, MD5
  `f24c1188bb5d6800917a3ee422745c24`, module and platform status `ready`.
- The full `ydb/udfs/wasm/types/query.sql` sample completed successfully,
  including nested containers, Optional, Variant and Callable checks.

Refresh evidence logs:

- `/tmp/wasm-integration-refresh-guests.log`
- `/tmp/wasm-integration-refresh-host.log`
- `/tmp/wasm-integration-refresh-tests.log`
- `/tmp/wasm-integration-refresh-sdk-upload.log`
- `/tmp/wasm-integration-refresh-types-upload.log`
- `/tmp/wasm-integration-refresh-types-query.json`
