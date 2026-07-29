#include "table_query.h"

#include "blob_chunks.h"
#include "metadata_subscription/udf_module.h"

#include <ydb/services/metadata/manager/ydb_value_operator.h>

#include <google/protobuf/any.pb.h>

#include <util/string/builder.h>
#include <util/string/subst.h>

namespace NKikimr::NUdfStore::NTableQuery {

namespace {

using namespace NMetadata::NInternal;

Ydb::TypedValue MakeUtf8Param(const TString& value) {
    Ydb::TypedValue result;
    result.mutable_type()->set_type_id(Ydb::Type::UTF8);
    result.mutable_value()->set_text_value(value);
    return result;
}

Ydb::TypedValue MakeStringParam(const TString& value) {
    Ydb::TypedValue result;
    result.mutable_type()->set_type_id(Ydb::Type::STRING);
    result.mutable_value()->set_bytes_value(value);
    return result;
}

Ydb::TypedValue MakeUint64Param(ui64 value) {
    Ydb::TypedValue result;
    result.mutable_type()->set_type_id(Ydb::Type::UINT64);
    result.mutable_value()->set_uint64_value(value);
    return result;
}

TString EscapeTablePath(const TString& tablePath) {
    // YQL backtick-quoted identifiers escape ` by doubling.
    TString escaped = tablePath;
    SubstGlobal(escaped, "`", "``");
    return escaped;
}

i32 FindColumnIndex(const Ydb::ResultSet& resultSet, const TString& columnName) {
    for (i32 i = 0; i < resultSet.columns_size(); ++i) {
        if (resultSet.columns(i).name() == columnName) {
            return i;
        }
    }
    return -1;
}

bool ReadUtf8Column(const Ydb::ResultSet& resultSet, const TString& columnName, TString& value) {
    if (resultSet.rows().empty()) {
        return false;
    }
    const i32 columnIdx = FindColumnIndex(resultSet, columnName);
    if (columnIdx < 0) {
        return false;
    }
    const auto& row = resultSet.rows(0);
    if (columnIdx >= row.items_size()) {
        return false;
    }
    const auto& item = row.items(columnIdx);
    if (!item.has_text_value()) {
        return false;
    }
    value = item.text_value();
    return true;
}

bool ReadUint64Column(const Ydb::ResultSet& resultSet, const TString& columnName, ui64& value) {
    if (resultSet.rows().empty()) {
        return false;
    }
    const i32 columnIdx = FindColumnIndex(resultSet, columnName);
    if (columnIdx < 0) {
        return false;
    }
    const auto& row = resultSet.rows(0);
    if (columnIdx >= row.items_size()) {
        return false;
    }
    const auto& item = row.items(columnIdx);
    if (!item.has_uint64_value()) {
        return false;
    }
    value = item.uint64_value();
    return true;
}

bool ParseChunksResultSet(const Ydb::ResultSet& resultSet, TVector<TString>& chunks) {
    const i32 chunkIdxCol = FindColumnIndex(resultSet, "chunk_idx");
    const i32 dataCol = FindColumnIndex(resultSet, "data");
    if (chunkIdxCol < 0 || dataCol < 0) {
        return false;
    }

    TMap<ui64, TString> ordered;
    for (const auto& row : resultSet.rows()) {
        if (chunkIdxCol >= row.items_size() || dataCol >= row.items_size()) {
            return false;
        }
        const auto& idxItem = row.items(chunkIdxCol);
        const auto& dataItem = row.items(dataCol);
        if (!idxItem.has_uint64_value() || !dataItem.has_bytes_value()) {
            return false;
        }
        ordered[idxItem.uint64_value()] = dataItem.bytes_value();
    }

    chunks.clear();
    chunks.reserve(ordered.size());
    ui64 expectedIdx = 0;
    for (const auto& [idx, data] : ordered) {
        if (idx != expectedIdx) {
            return false;
        }
        chunks.push_back(data);
        ++expectedIdx;
    }
    return true;
}

} // namespace

bool ExtractQueryResult(
    const Ydb::Table::ExecuteDataQueryResponse& response,
    Ydb::Table::ExecuteQueryResult& result)
{
    if (response.operation().status() != Ydb::StatusIds::SUCCESS) {
        return false;
    }
    if (!response.operation().result().Is<Ydb::Table::ExecuteQueryResult>()) {
        return false;
    }
    response.operation().result().UnpackTo(&result);
    return result.result_sets_size() > 0;
}

TString BuildSelectModuleByMd5Query(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $md5 AS Utf8; "
        << "SELECT uid, md5, name, type, version, size, chunk_count, compile_status, compile_error FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE md5 = $md5 AND type = 'WASM';";
}

void SetSelectModuleByMd5Params(Ydb::Table::ExecuteDataQueryRequest& request, const TString& md5) {
    (*request.mutable_parameters())["$md5"] = MakeUtf8Param(md5);
}

TString BuildSelectModuleByNameQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $name AS Utf8; "
        << "SELECT uid, md5, name, type, version, size, chunk_count, compile_status, compile_error FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE name = $name AND type = 'LIBRARY';";
}

void SetSelectModuleByNameParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& name) {
    (*request.mutable_parameters())["$name"] = MakeUtf8Param(name);
}

bool ParseModuleSourceResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TModuleSourceRow& row) {
    Ydb::Table::ExecuteQueryResult result;
    if (!ExtractQueryResult(response, result)) {
        return false;
    }
    const auto& resultSet = result.result_sets(0);
    if (resultSet.rows().empty()) {
        return false;
    }
    if (!ReadUtf8Column(resultSet, "uid", row.Uid)) {
        return false;
    }
    ReadUtf8Column(resultSet, "md5", row.Md5);
    ReadUtf8Column(resultSet, "name", row.Name);
    ReadUtf8Column(resultSet, "type", row.Type);
    ReadUint64Column(resultSet, "version", row.Version);
    ReadUint64Column(resultSet, "size", row.Size);
    ReadUint64Column(resultSet, "chunk_count", row.ChunkCount);
    TString compileStatus;
    if (ReadUtf8Column(resultSet, "compile_status", compileStatus)) {
        TUdfModule::CompileStatusFromString(compileStatus, row.CompileStatus);
    }
    ReadUtf8Column(resultSet, "compile_error", row.CompileError);
    ReadUtf8Column(resultSet, "manifest", row.Manifest);
    return true;
}

TString BuildSelectSourceChunksQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $owner_key AS Utf8; "
        << "SELECT chunk_idx, data FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE owner_key = $owner_key ORDER BY chunk_idx;";
}

void SetSelectSourceChunksParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& ownerKey) {
    (*request.mutable_parameters())["$owner_key"] = MakeUtf8Param(ownerKey);
}

bool ParseSourceChunksResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TVector<TString>& chunks) {
    Ydb::Table::ExecuteQueryResult result;
    if (!ExtractQueryResult(response, result)) {
        return false;
    }
    return ParseChunksResultSet(result.result_sets(0), chunks);
}

TString BuildSelectArtifactQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $id AS Utf8; "
        << "DECLARE $kind AS Utf8; "
        << "SELECT id, kind, source_md5, version, format, "
        << "wasm_data_size, wasm_data_chunk_count, object_code_size, object_code_chunk_count FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE id = $id AND kind = $kind;";
}

void SetSelectArtifactParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind)
{
    (*request.mutable_parameters())["$id"] = MakeUtf8Param(id);
    (*request.mutable_parameters())["$kind"] = MakeUtf8Param(kind);
}

bool ParseArtifactResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TWasmArtifactRow& row) {
    Ydb::Table::ExecuteQueryResult result;
    if (!ExtractQueryResult(response, result)) {
        return false;
    }
    const auto& resultSet = result.result_sets(0);
    if (resultSet.rows().empty()) {
        return false;
    }
    if (!ReadUtf8Column(resultSet, "id", row.Id)) {
        return false;
    }
    ReadUtf8Column(resultSet, "kind", row.Kind);
    ReadUtf8Column(resultSet, "source_md5", row.SourceMd5);
    ReadUint64Column(resultSet, "version", row.Version);
    ReadUtf8Column(resultSet, "format", row.Format);
    ReadUint64Column(resultSet, "wasm_data_size", row.WasmDataSize);
    ReadUint64Column(resultSet, "wasm_data_chunk_count", row.WasmDataChunkCount);
    ReadUint64Column(resultSet, "object_code_size", row.ObjectCodeSize);
    ReadUint64Column(resultSet, "object_code_chunk_count", row.ObjectCodeChunkCount);
    return row.ObjectCodeChunkCount > 0;
}

TString BuildSelectArtifactChunksQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $id AS Utf8; "
        << "DECLARE $kind AS Utf8; "
        << "DECLARE $blob_kind AS Utf8; "
        << "SELECT chunk_idx, data FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE id = $id AND kind = $kind AND blob_kind = $blob_kind ORDER BY chunk_idx;";
}

void SetSelectArtifactChunksParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind,
    const TString& blobKind)
{
    (*request.mutable_parameters())["$id"] = MakeUtf8Param(id);
    (*request.mutable_parameters())["$kind"] = MakeUtf8Param(kind);
    (*request.mutable_parameters())["$blob_kind"] = MakeUtf8Param(blobKind);
}

bool ParseArtifactChunksResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TVector<TString>& chunks) {
    Ydb::Table::ExecuteQueryResult result;
    if (!ExtractQueryResult(response, result)) {
        return false;
    }
    return ParseChunksResultSet(result.result_sets(0), chunks);
}

TString BuildUpsertArtifactQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $id AS Utf8; "
        << "DECLARE $kind AS Utf8; "
        << "DECLARE $source_md5 AS Utf8; "
        << "DECLARE $version AS Uint64; "
        << "DECLARE $format AS Utf8; "
        << "DECLARE $wasm_data_size AS Uint64; "
        << "DECLARE $wasm_data_chunk_count AS Uint64; "
        << "DECLARE $object_code_size AS Uint64; "
        << "DECLARE $object_code_chunk_count AS Uint64; "
        << "UPSERT INTO `"
        << EscapeTablePath(tablePath)
        << "` (id, kind, source_md5, version, format, "
        << "wasm_data_size, wasm_data_chunk_count, object_code_size, object_code_chunk_count, compiled_at) "
        << "VALUES ($id, $kind, $source_md5, $version, $format, "
        << "$wasm_data_size, $wasm_data_chunk_count, $object_code_size, $object_code_chunk_count, CurrentUtcTimestamp());";
}

void SetUpsertArtifactParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TWasmArtifactRow& row)
{
    (*request.mutable_parameters())["$id"] = MakeUtf8Param(row.Id);
    (*request.mutable_parameters())["$kind"] = MakeUtf8Param(row.Kind);
    (*request.mutable_parameters())["$source_md5"] = MakeUtf8Param(row.SourceMd5);
    (*request.mutable_parameters())["$version"] = MakeUint64Param(row.Version);
    (*request.mutable_parameters())["$format"] = MakeUtf8Param(row.Format);
    (*request.mutable_parameters())["$wasm_data_size"] = MakeUint64Param(row.WasmDataSize);
    (*request.mutable_parameters())["$wasm_data_chunk_count"] = MakeUint64Param(row.WasmDataChunkCount);
    (*request.mutable_parameters())["$object_code_size"] = MakeUint64Param(row.ObjectCodeSize);
    (*request.mutable_parameters())["$object_code_chunk_count"] = MakeUint64Param(row.ObjectCodeChunkCount);
}

TString BuildDeleteArtifactChunksQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $id AS Utf8; "
        << "DECLARE $kind AS Utf8; "
        << "DELETE FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE id = $id AND kind = $kind;";
}

void SetDeleteArtifactChunksParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind)
{
    (*request.mutable_parameters())["$id"] = MakeUtf8Param(id);
    (*request.mutable_parameters())["$kind"] = MakeUtf8Param(kind);
}

TString BuildUpsertArtifactChunkQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $id AS Utf8; "
        << "DECLARE $kind AS Utf8; "
        << "DECLARE $blob_kind AS Utf8; "
        << "DECLARE $chunk_idx AS Uint64; "
        << "DECLARE $data AS String; "
        << "UPSERT INTO `"
        << EscapeTablePath(tablePath)
        << "` (id, kind, blob_kind, chunk_idx, data) "
        << "VALUES ($id, $kind, $blob_kind, $chunk_idx, $data);";
}

void SetUpsertArtifactChunkParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind,
    const TString& blobKind,
    ui64 chunkIdx,
    const TString& data)
{
    (*request.mutable_parameters())["$id"] = MakeUtf8Param(id);
    (*request.mutable_parameters())["$kind"] = MakeUtf8Param(kind);
    (*request.mutable_parameters())["$blob_kind"] = MakeUtf8Param(blobKind);
    (*request.mutable_parameters())["$chunk_idx"] = MakeUint64Param(chunkIdx);
    (*request.mutable_parameters())["$data"] = MakeStringParam(data);
}

TString BuildUpdateCompileStatusQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $uid AS Utf8; "
        << "DECLARE $compile_status AS Utf8; "
        << "DECLARE $compile_error AS Utf8; "
        << "UPDATE `"
        << EscapeTablePath(tablePath)
        << "` SET compile_status = $compile_status, "
        << "compile_error = $compile_error, "
        << "compile_started_at = IF($compile_status = 'compiling', CurrentUtcTimestamp(), compile_started_at), "
        << "compile_finished_at = IF($compile_status = 'ready' OR $compile_status = 'failed', "
        << "CurrentUtcTimestamp(), compile_finished_at) "
        << "WHERE uid = $uid;";
}

void SetUpdateCompileStatusParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& uid,
    const TString& status,
    const TString& errorMessage)
{
    (*request.mutable_parameters())["$uid"] = MakeUtf8Param(uid);
    (*request.mutable_parameters())["$compile_status"] = MakeUtf8Param(status);
    (*request.mutable_parameters())["$compile_error"] = MakeUtf8Param(errorMessage);
}

Ydb::TypedValue MakeJsonParam(const TString& value) {
    Ydb::TypedValue result;
    result.mutable_type()->set_type_id(Ydb::Type::JSON);
    result.mutable_value()->set_text_value(value);
    return result;
}

TString BuildSelectModuleRowByMd5Query(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $md5 AS Utf8; "
        << "SELECT uid, md5, name, type, version, size, chunk_count, compile_status, compile_error, CAST(manifest AS Utf8) AS manifest FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE md5 = $md5 LIMIT 1;";
}

void SetSelectModuleRowByMd5Params(Ydb::Table::ExecuteDataQueryRequest& request, const TString& md5) {
    (*request.mutable_parameters())["$md5"] = MakeUtf8Param(md5);
}

TString BuildSelectModuleRowByNameAndTypeQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $name AS Utf8; "
        << "DECLARE $type AS Utf8; "
        << "SELECT uid, md5, name, type, version, size, chunk_count, compile_status, compile_error, CAST(manifest AS Utf8) AS manifest FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE name = $name AND type = $type LIMIT 1;";
}

void SetSelectModuleRowByNameAndTypeParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& name,
    const TString& type)
{
    (*request.mutable_parameters())["$name"] = MakeUtf8Param(name);
    (*request.mutable_parameters())["$type"] = MakeUtf8Param(type);
}

TString BuildListModulesQuery(
    const TString& tablePath,
    bool filterType,
    bool filterNamePrefix,
    ui64 limit)
{
    TStringBuilder sb;
    if (filterType) {
        sb << "DECLARE $type AS Utf8; ";
    }
    if (filterNamePrefix) {
        sb << "DECLARE $name_prefix AS Utf8; ";
    }
    sb << "SELECT uid, md5, name, type, version, size, chunk_count, compile_status, compile_error, CAST(manifest AS Utf8) AS manifest FROM `"
       << EscapeTablePath(tablePath)
       << "`";
    TVector<TString> where;
    if (filterType) {
        where.push_back("type = $type");
    }
    if (filterNamePrefix) {
        where.push_back("StartsWith(name, $name_prefix)");
    }
    if (!where.empty()) {
        sb << " WHERE " << where[0];
        for (size_t i = 1; i < where.size(); ++i) {
            sb << " AND " << where[i];
        }
    }
    sb << " ORDER BY name, type, md5";
    if (limit > 0) {
        sb << " LIMIT " << limit;
    }
    sb << ";";
    return sb;
}

void SetListModulesParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& type,
    const TString& namePrefix)
{
    if (type) {
        (*request.mutable_parameters())["$type"] = MakeUtf8Param(type);
    }
    if (namePrefix) {
        (*request.mutable_parameters())["$name_prefix"] = MakeUtf8Param(namePrefix);
    }
}

bool ParseModuleRowsResponse(
    const Ydb::Table::ExecuteDataQueryResponse& response,
    TVector<TModuleSourceRow>& rows)
{
    Ydb::Table::ExecuteQueryResult result;
    if (!ExtractQueryResult(response, result)) {
        rows.clear();
        return true; // empty success
    }
    const auto& resultSet = result.result_sets(0);
    rows.clear();
    rows.reserve(resultSet.rows_size());
    for (i32 i = 0; i < resultSet.rows_size(); ++i) {
        // Reuse single-row helpers by synthesizing a 1-row ResultSet view is awkward;
        // parse columns directly.
        TModuleSourceRow row;
        const i32 uidCol = FindColumnIndex(resultSet, "uid");
        const i32 md5Col = FindColumnIndex(resultSet, "md5");
        const i32 nameCol = FindColumnIndex(resultSet, "name");
        const i32 typeCol = FindColumnIndex(resultSet, "type");
        const i32 versionCol = FindColumnIndex(resultSet, "version");
        const i32 sizeCol = FindColumnIndex(resultSet, "size");
        const i32 chunkCol = FindColumnIndex(resultSet, "chunk_count");
        const i32 statusCol = FindColumnIndex(resultSet, "compile_status");
        const i32 errorCol = FindColumnIndex(resultSet, "compile_error");
        const i32 manifestCol = FindColumnIndex(resultSet, "manifest");
        if (uidCol < 0) {
            return false;
        }
        const auto& rowMsg = resultSet.rows(i);
        auto readText = [&](i32 col, TString& out) {
            if (col >= 0 && col < rowMsg.items_size() && rowMsg.items(col).has_text_value()) {
                out = rowMsg.items(col).text_value();
            }
        };
        auto readUint = [&](i32 col, ui64& out) {
            if (col >= 0 && col < rowMsg.items_size() && rowMsg.items(col).has_uint64_value()) {
                out = rowMsg.items(col).uint64_value();
            }
        };
        readText(uidCol, row.Uid);
        readText(md5Col, row.Md5);
        readText(nameCol, row.Name);
        readText(typeCol, row.Type);
        readUint(versionCol, row.Version);
        readUint(sizeCol, row.Size);
        readUint(chunkCol, row.ChunkCount);
        TString status;
        readText(statusCol, status);
        if (status) {
            TUdfModule::CompileStatusFromString(status, row.CompileStatus);
        }
        readText(errorCol, row.CompileError);
        readText(manifestCol, row.Manifest);
        rows.push_back(std::move(row));
    }
    return true;
}

TString BuildUpsertModuleRowQuery(const TString& tablePath, bool withManifest, bool withCompileStatus) {
    TStringBuilder decls;
    decls << "DECLARE $uid AS Utf8; "
          << "DECLARE $md5 AS Utf8; "
          << "DECLARE $size AS Uint64; "
          << "DECLARE $name AS Utf8; "
          << "DECLARE $type AS Utf8; "
          << "DECLARE $version AS Uint64; "
          << "DECLARE $chunk_count AS Uint64; ";
    TString columns = "uid, md5, size, name, type, version, chunk_count, created_at";
    TString values = "$uid, $md5, $size, $name, $type, $version, $chunk_count, CurrentUtcTimestamp()";
    if (withCompileStatus) {
        decls << "DECLARE $compile_status AS Utf8; ";
        columns += ", compile_status";
        values += ", $compile_status";
    }
    if (withManifest) {
        decls << "DECLARE $manifest AS Json; ";
        columns += ", manifest";
        values += ", $manifest";
    }
    return TStringBuilder()
        << decls
        << "UPSERT INTO `"
        << EscapeTablePath(tablePath)
        << "` (" << columns << ") VALUES (" << values << ");";
}

void SetUpsertModuleRowParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TUpsertModuleRow& row)
{
    (*request.mutable_parameters())["$uid"] = MakeUtf8Param(row.Uid);
    (*request.mutable_parameters())["$md5"] = MakeUtf8Param(row.Md5);
    (*request.mutable_parameters())["$size"] = MakeUint64Param(row.Size);
    (*request.mutable_parameters())["$name"] = MakeUtf8Param(row.Name);
    (*request.mutable_parameters())["$type"] = MakeUtf8Param(row.Type);
    (*request.mutable_parameters())["$version"] = MakeUint64Param(row.Version);
    (*request.mutable_parameters())["$chunk_count"] = MakeUint64Param(row.ChunkCount);
    if (row.CompileStatus) {
        (*request.mutable_parameters())["$compile_status"] = MakeUtf8Param(row.CompileStatus);
    }
    if (row.Manifest) {
        (*request.mutable_parameters())["$manifest"] = MakeJsonParam(row.Manifest);
    }
}

TString BuildUpsertModuleChunkQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $owner_key AS Utf8; "
        << "DECLARE $chunk_idx AS Uint64; "
        << "DECLARE $data AS String; "
        << "UPSERT INTO `"
        << EscapeTablePath(tablePath)
        << "` (owner_key, chunk_idx, data) "
        << "VALUES ($owner_key, $chunk_idx, $data);";
}

void SetUpsertModuleChunkParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& ownerKey,
    ui64 chunkIdx,
    const TString& data)
{
    (*request.mutable_parameters())["$owner_key"] = MakeUtf8Param(ownerKey);
    (*request.mutable_parameters())["$chunk_idx"] = MakeUint64Param(chunkIdx);
    (*request.mutable_parameters())["$data"] = MakeStringParam(data);
}

TString BuildDeleteModuleChunksQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $owner_key AS Utf8; "
        << "DELETE FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE owner_key = $owner_key;";
}

void SetDeleteModuleChunksParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& ownerKey) {
    (*request.mutable_parameters())["$owner_key"] = MakeUtf8Param(ownerKey);
}

TString BuildDeleteModuleByUidQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $uid AS Utf8; "
        << "DELETE FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE uid = $uid;";
}

void SetDeleteModuleByUidParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& uid) {
    (*request.mutable_parameters())["$uid"] = MakeUtf8Param(uid);
}

TString BuildDeleteArtifactRowQuery(const TString& tablePath) {
    return TStringBuilder()
        << "DECLARE $id AS Utf8; "
        << "DECLARE $kind AS Utf8; "
        << "DELETE FROM `"
        << EscapeTablePath(tablePath)
        << "` WHERE id = $id AND kind = $kind;";
}

void SetDeleteArtifactRowParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind)
{
    (*request.mutable_parameters())["$id"] = MakeUtf8Param(id);
    (*request.mutable_parameters())["$kind"] = MakeUtf8Param(kind);
}

} // namespace NKikimr::NUdfStore::NTableQuery
