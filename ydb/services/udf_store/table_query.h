#pragma once

#include "metadata_subscription/udf_module.h"

#include <ydb/public/api/protos/ydb_table.pb.h>

#include <util/generic/string.h>
#include <util/generic/vector.h>

namespace NKikimr::NUdfStore::NTableQuery {

struct TModuleSourceRow {
    TString Uid;
    TString Md5;
    TString Name;
    TString Type;
    ui64 Version = 0;
    ui64 Size = 0;
    ui64 ChunkCount = 0;
    TString Body;
    TString Manifest;
    ECompileStatus CompileStatus = ECompileStatus::Pending;
    TString CompileError;
};

struct TWasmArtifactRow {
    TString Id;
    TString Kind;
    TString SourceMd5;
    ui64 Version = 0;
    TString Format;
    ui64 WasmDataSize = 0;
    ui64 WasmDataChunkCount = 0;
    ui64 ObjectCodeSize = 0;
    ui64 ObjectCodeChunkCount = 0;
    TString WasmData;
    TString ObjectCode;
};

struct TPendingChunkWrite {
    TString BlobKind;
    ui64 ChunkIdx = 0;
    TString Data;
};

TString BuildSelectModuleByMd5Query(const TString& tablePath);
void SetSelectModuleByMd5Params(Ydb::Table::ExecuteDataQueryRequest& request, const TString& md5);

TString BuildSelectModuleByNameQuery(const TString& tablePath);
void SetSelectModuleByNameParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& name);

bool ParseModuleSourceResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TModuleSourceRow& row);

TString BuildSelectSourceChunksQuery(const TString& tablePath);
void SetSelectSourceChunksParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& ownerKey);
bool ParseSourceChunksResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TVector<TString>& chunks);

TString BuildSelectArtifactQuery(const TString& tablePath);
void SetSelectArtifactParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind);
bool ParseArtifactResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TWasmArtifactRow& row);

TString BuildSelectArtifactChunksQuery(const TString& tablePath);
void SetSelectArtifactChunksParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind,
    const TString& blobKind);
bool ParseArtifactChunksResponse(const Ydb::Table::ExecuteDataQueryResponse& response, TVector<TString>& chunks);

TString BuildUpsertArtifactQuery(const TString& tablePath);
void SetUpsertArtifactParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TWasmArtifactRow& row);

TString BuildDeleteArtifactChunksQuery(const TString& tablePath);
void SetDeleteArtifactChunksParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind);

TString BuildUpsertArtifactChunkQuery(const TString& tablePath);
void SetUpsertArtifactChunkParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind,
    const TString& blobKind,
    ui64 chunkIdx,
    const TString& data);

TString BuildUpdateCompileStatusQuery(const TString& tablePath);
void SetUpdateCompileStatusParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& uid,
    const TString& status,
    const TString& errorMessage);

// --- module upload / delete / describe / list ---

struct TUpsertModuleRow {
    TString Uid;
    TString Md5;
    TString Name;
    TString Type;
    ui64 Version = 1;
    ui64 Size = 0;
    ui64 ChunkCount = 0;
    TString Manifest;
    TString CompileStatus; // empty = omit column (native)
};

TString BuildSelectModuleRowByMd5Query(const TString& tablePath);
void SetSelectModuleRowByMd5Params(Ydb::Table::ExecuteDataQueryRequest& request, const TString& md5);

TString BuildSelectModuleRowByNameAndTypeQuery(const TString& tablePath);
void SetSelectModuleRowByNameAndTypeParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& name,
    const TString& type);

TString BuildListModulesQuery(
    const TString& tablePath,
    bool filterType,
    bool filterNamePrefix,
    ui64 limit);

void SetListModulesParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& type,
    const TString& namePrefix);

bool ParseModuleRowsResponse(
    const Ydb::Table::ExecuteDataQueryResponse& response,
    TVector<TModuleSourceRow>& rows);

TString BuildUpsertModuleRowQuery(const TString& tablePath, bool withManifest, bool withCompileStatus);
void SetUpsertModuleRowParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TUpsertModuleRow& row);

TString BuildUpsertModuleChunkQuery(const TString& tablePath);
void SetUpsertModuleChunkParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& ownerKey,
    ui64 chunkIdx,
    const TString& data);

TString BuildDeleteModuleChunksQuery(const TString& tablePath);
void SetDeleteModuleChunksParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& ownerKey);

TString BuildDeleteModuleByUidQuery(const TString& tablePath);
void SetDeleteModuleByUidParams(Ydb::Table::ExecuteDataQueryRequest& request, const TString& uid);

TString BuildDeleteArtifactRowQuery(const TString& tablePath);
void SetDeleteArtifactRowParams(
    Ydb::Table::ExecuteDataQueryRequest& request,
    const TString& id,
    const TString& kind);

bool ExtractQueryResult(
    const Ydb::Table::ExecuteDataQueryResponse& response,
    Ydb::Table::ExecuteQueryResult& result);

} // namespace NKikimr::NUdfStore::NTableQuery
