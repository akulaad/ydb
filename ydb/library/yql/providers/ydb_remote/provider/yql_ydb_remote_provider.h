#pragma once

#include <ydb/library/yql/providers/common/token_accessor/client/factory.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/driver/driver.h>
#include <yql/essentials/core/yql_data_provider.h>

namespace NYql {

TDataProviderInfo CreateYdbRemoteDataProviders(
    TTypeAnnotationContext* types,
    const NYdb::TDriver& driver,
    IStructuredTokenCredentialsFactory::TPtr credentialsFactory = CreateStructuredTokenCredentialsFactory());

} // namespace NYql
