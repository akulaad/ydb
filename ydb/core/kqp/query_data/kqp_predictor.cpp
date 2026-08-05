#include "kqp_predictor.h"
#include "kqp_request_predictor.h"

#include <ydb/core/base/appdata.h>
#include <yql/essentials/core/yql_expr_optimize.h>
#include <yql/essentials/core/yql_expr_type_annotation.h>
#include <yql/essentials/core/yql_type_annotation.h>
#include <util/system/info.h>
#include <ydb/library/yql/dq/expr_nodes/dq_expr_nodes.h>
#include <yql/essentials/core/expr_nodes/yql_expr_nodes.h>
#include <ydb/core/kqp/expr_nodes/kqp_expr_nodes.h>
#include <ydb/core/kqp/common/kqp_yql.h>
#include <ydb/library/actors/core/subsystems/stats.h>
#include <ydb/library/services/services.pb.h>

#include <util/generic/algorithm.h>
#include <util/generic/hash.h>
#include <util/string/cast.h>

#include <cmath>
#include <algorithm>

#define YDB_LOG_THIS_FILE_COMPONENT NKikimrServices::KQP_EXECUTER

namespace NKikimr::NKqp {

using namespace NActors;
using namespace NYql;
using namespace NYql::NNodes;

namespace {

bool IsStringLikeDataSlot(NUdf::EDataSlot slot) {
    switch (slot) {
        case NUdf::EDataSlot::String:
        case NUdf::EDataSlot::Utf8:
        case NUdf::EDataSlot::Yson:
        case NUdf::EDataSlot::Json:
        case NUdf::EDataSlot::JsonDocument:
        case NUdf::EDataSlot::DyNumber:
        case NUdf::EDataSlot::Uuid:
            return true;
        default:
            return false;
    }
}

bool IsStringLikeTypeAnn(const TTypeAnnotationNode* type) {
    if (!type) {
        return false;
    }
    bool isOptional = false;
    const TDataExprType* dataType = nullptr;
    if (!IsDataOrOptionalOfData(type, isOptional, dataType) || !dataType) {
        return false;
    }
    return IsStringLikeDataSlot(dataType->GetSlot());
}

const TExprNode* PeelTrivialWrappers(const TExprNode* node) {
    while (node) {
        TExprBase base(node);
        if (auto just = base.Maybe<TCoJust>()) {
            node = just.Cast().Input().Raw();
        } else if (auto unwrap = base.Maybe<TCoUnwrap>()) {
            node = unwrap.Cast().Optional().Raw();
        } else if (auto coalesce = base.Maybe<TCoCoalesce>()) {
            // Prefer the primary branch for column detection.
            node = coalesce.Cast().Predicate().Raw();
        } else if (auto cast = base.Maybe<TCoSafeCast>()) {
            node = cast.Cast().Value().Raw();
        } else {
            break;
        }
    }
    return node;
}

const TExprNode* PeelFlowWrappers(const TExprNode* node) {
    while (node) {
        TExprBase base(node);
        if (auto toFlow = base.Maybe<TCoToFlow>()) {
            node = toFlow.Cast().Input().Raw();
        } else if (auto fromFlow = base.Maybe<TCoFromFlow>()) {
            node = fromFlow.Cast().Input().Raw();
        } else {
            break;
        }
    }
    return node;
}

TMaybeNode<TCoAtomList> TryGetWideReadColumns(TExprBase input) {
    if (auto read = input.Maybe<TKqpWideReadTable>()) {
        return read.Cast().Columns();
    }
    if (auto read = input.Maybe<TKqpWideReadTableRanges>()) {
        return read.Cast().Columns();
    }
    if (auto read = input.Maybe<TKqpWideReadOlapTableRanges>()) {
        return read.Cast().Columns();
    }
    return {};
}

//! WideMap(ExpandMap(_, λ → Member(row, c0), Member(row, c1), ...), λ(a0, a1, ...))
//! maps a_i → c_i. Used by RBO / source-stage physical plans.
bool TryGetExpandMapMemberColumns(TExprBase input, TVector<TString>& columns) {
    const TExprNode* node = PeelFlowWrappers(input.Raw());
    if (!node || !node->IsCallable("ExpandMap") || node->ChildrenSize() < 2) {
        return false;
    }
    const TExprNode& lambda = *node->Child(1);
    if (!lambda.IsLambda() || lambda.ChildrenSize() < 2) {
        return false;
    }
    // Lambda: Child(0)=args, Child(1..)=body items (Member for each column).
    columns.clear();
    columns.reserve(lambda.ChildrenSize() - 1);
    for (ui32 i = 1; i < lambda.ChildrenSize(); ++i) {
        TExprBase bodyItem(lambda.Child(i));
        auto member = bodyItem.Maybe<TCoMember>();
        if (!member) {
            columns.clear();
            return false;
        }
        columns.emplace_back(TString(member.Cast().Name().Value()));
    }
    return !columns.empty();
}

void RegisterWideMapColumnArgs(
    TExprBase node,
    THashMap<const TExprNode*, TString>& argToColumn)
{
    auto wideMap = node.Maybe<TCoWideMap>();
    if (!wideMap) {
        return;
    }
    const auto lambda = wideMap.Cast().Lambda();
    const auto args = lambda.Args();

    if (auto columns = TryGetWideReadColumns(wideMap.Cast().Input())) {
        const auto cols = columns.Cast();
        const ui32 n = Min<ui32>(args.Size(), cols.Size());
        for (ui32 i = 0; i < n; ++i) {
            argToColumn[args.Arg(i).Raw()] = TString(cols.Item(i).Value());
        }
        return;
    }

    TVector<TString> expandColumns;
    if (TryGetExpandMapMemberColumns(wideMap.Cast().Input(), expandColumns)) {
        const ui32 n = Min<ui32>(args.Size(), expandColumns.size());
        for (ui32 i = 0; i < n; ++i) {
            argToColumn[args.Arg(i).Raw()] = expandColumns[i];
        }
    }
}

bool AllowStringColumnArg(const TExprNode* argNode) {
    if (!argNode) {
        return false;
    }
    // Phy plans sometimes lack type ann on Member/Argument; still accept.
    // If ann is present, require string-like.
    if (!argNode->GetTypeAnn()) {
        return true;
    }
    return IsStringLikeTypeAnn(argNode->GetTypeAnn());
}

void CollectStringColumnsFromApply(
    TExprBase node,
    const THashMap<const TExprNode*, TString>& argToColumn,
    THashSet<TString>& outColumns)
{
    auto apply = node.Maybe<TCoApply>();
    if (!apply) {
        return;
    }
    auto callable = apply.Cast().Callable();
    if (!callable.Maybe<TCoUdf>() && !callable.Maybe<TCoScriptUdf>()) {
        return;
    }

    // Apply children: 0 = callable, 1.. = args
    const auto& ref = apply.Cast().Ref();
    for (ui32 i = 1; i < ref.ChildrenSize(); ++i) {
        const TExprNode* argNode = PeelTrivialWrappers(ref.Child(i));
        if (!AllowStringColumnArg(argNode)) {
            continue;
        }
        TExprBase arg(argNode);
        if (auto member = arg.Maybe<TCoMember>()) {
            outColumns.insert(TString(member.Cast().Name().Value()));
            continue;
        }
        if (auto argument = arg.Maybe<TCoArgument>()) {
            if (const TString* name = argToColumn.FindPtr(argument.Cast().Raw())) {
                outColumns.insert(*name);
            }
        }
    }
}

} // namespace

void TStagePredictor::Prepare() {
    InputDataPrediction = 1;
    if (HasRangeScanFlag) {
        InputDataPrediction = 1;
    } else if (InputDataVolumes.size()) {
        InputDataPrediction = 0;
    }

    for (auto&& i : InputDataVolumes) {
        InputDataPrediction += i;
    }

    OutputDataPrediction = InputDataPrediction;
    if (HasTopFlag) {
        OutputDataPrediction = InputDataPrediction * 0.01;
    } else if (HasStateCombinerFlag || HasFinalCombinerFlag) {
        if (GroupByKeysCount) {
            OutputDataPrediction = InputDataPrediction;
        } else {
            OutputDataPrediction = InputDataPrediction * 0.01;
        }
    }
}

void TStagePredictor::Scan(const NYql::TExprNode::TPtr& stageNode) {
    THashMap<const TExprNode*, TString> wideArgToColumn;
    NYql::VisitExpr(stageNode, [&](const NYql::TExprNode::TPtr& exprNode) {
        NYql::NNodes::TExprBase node(exprNode);
        ++NodesCount;
        if (node.Maybe<NYql::NNodes::TCoCondense>() || node.Ref().Content() == "WideCondense1" || node.Maybe<NYql::NNodes::TCoCondense1>()) {
            HasCondenseFlag = true;
        } else if (node.Maybe<NYql::NNodes::TKqpWideReadTable>()) {
            HasRangeScanFlag = true;
        } else if (node.Maybe<NYql::NNodes::TKqpWideReadTableRanges>() || node.Maybe<NYql::NNodes::TKqpWideReadOlapTableRanges>()) {
            HasRangeScanFlag = true;
        } else if (node.Maybe<NYql::NNodes::TCoSort>()) {
            HasSortFlag = true;
        } else if (node.Maybe<NYql::NNodes::TCoKeepTop>() || node.Maybe<NYql::NNodes::TCoTop>() || node.Maybe<NYql::NNodes::TCoWideTop>()) {
            HasTopFlag = true;
        } else if (node.Maybe<NYql::NNodes::TCoTopSort>() || node.Maybe<NYql::NNodes::TCoWideTopSort>()) {
            HasTopFlag = true;
            HasSortFlag = true;
        } else if (node.Maybe<NYql::NNodes::TCoFilterBase>()) {
            HasFilterFlag = true;
        } else if (node.Maybe<NYql::NNodes::TCoWideCombiner>()) {
            auto wCombiner = node.Cast<NYql::NNodes::TCoWideCombiner>();
            GroupByKeysCount = wCombiner.KeyExtractor().Ptr()->ChildrenSize() - 1;
            if (wCombiner.MemLimit() != "") {
                HasFinalCombinerFlag = true;
            } else {
                HasStateCombinerFlag = true;
            }
        } else if (node.Maybe<NYql::NNodes::TCoMapJoinCore>()) {
            HasMapJoinFlag = true;
        } else if (const auto maybeWatermarkGenerator = node.Maybe<NYql::NNodes::TDqPhyWatermarkGenerator>()) {
            HasWatermarkGeneratorFlag = true;

            const auto watermarkGenerator = maybeWatermarkGenerator.Cast();
            for (const auto& nameValue : watermarkGenerator.WatermarkSettings()) {
                if (nameValue.Name().Value() != "WatermarksIdleTimeoutUs") {
                    continue;
                }

                ui64 idleTimeoutUs = 0;
                if (TryFromString<ui64>(nameValue.Value().Cast<NYql::NNodes::TCoAtom>().Value(), idleTimeoutUs)) {
                    WatermarkGeneratorIdleTimeoutUs = Max(WatermarkGeneratorIdleTimeoutUs.value_or(0), idleTimeoutUs);
                }
            }
        } else if (node.Maybe<NYql::NNodes::TCoUdf>()) {
            HasUdfFlag = true;
            const auto methodName = node.Cast<NYql::NNodes::TCoUdf>().MethodName().Value();
            TStringBuf moduleName;
            TStringBuf funcName;
            if (NYql::SplitUdfName(methodName, moduleName, funcName) && !moduleName.empty()) {
                WasmUdfModules_.insert(TString(moduleName));
            }
        }

        RegisterWideMapColumnArgs(node, wideArgToColumn);
        CollectStringColumnsFromApply(node, wideArgToColumn, WasmUdfStringColumns_);
        return true;
        });
}

void TStagePredictor::AcceptInputStageInfo(const TStagePredictor& info, const NYql::NNodes::TDqConnection& /*connection*/) {
    StageLevel = Max<ui32>(StageLevel, info.StageLevel + 1);
    InputDataVolumes.emplace_back(info.GetOutputDataPrediction());
}

void TStagePredictor::SerializeToKqpSettings(NYql::NDqProto::TProgram::TSettings& kqpProto) const {
    kqpProto.SetHasMapJoin(HasMapJoinFlag);
    kqpProto.SetHasSort(HasSortFlag);
    kqpProto.SetHasUdf(HasUdfFlag);
    kqpProto.SetHasFinalAggregation(HasFinalCombinerFlag);
    kqpProto.SetHasStateAggregation(HasStateCombinerFlag);
    kqpProto.SetGroupByKeysCount(GroupByKeysCount);
    kqpProto.SetHasFilter(HasFilterFlag);
    kqpProto.SetHasTop(HasTopFlag);
    kqpProto.SetHasRangeScan(HasRangeScanFlag);
    kqpProto.SetHasCondense(HasCondenseFlag);
    kqpProto.SetHasWatermarkGenerator(HasWatermarkGeneratorFlag);
    if (WatermarkGeneratorIdleTimeoutUs) {
        kqpProto.SetWatermarkGeneratorIdleTimeoutUs(*WatermarkGeneratorIdleTimeoutUs);
    }
    kqpProto.SetNodesCount(NodesCount);
    kqpProto.SetInputDataPrediction(InputDataPrediction);
    kqpProto.SetOutputDataPrediction(OutputDataPrediction);
    kqpProto.SetStageLevel(StageLevel);
    kqpProto.SetLevelDataPrediction(LevelDataPrediction.value_or(1));
    kqpProto.ClearWasmUdfModules();
    for (const auto& module : GetWasmUdfModules()) {
        kqpProto.AddWasmUdfModules(module);
    }
    kqpProto.ClearWasmUdfStringColumns();
    for (const auto& column : GetWasmUdfStringColumns()) {
        kqpProto.AddWasmUdfStringColumns(column);
    }
}

bool TStagePredictor::DeserializeFromKqpSettings(const NYql::NDqProto::TProgram::TSettings& kqpProto) {
    HasMapJoinFlag = kqpProto.GetHasMapJoin();
    HasSortFlag = kqpProto.GetHasSort();
    HasUdfFlag = kqpProto.GetHasUdf();
    HasFinalCombinerFlag = kqpProto.GetHasFinalAggregation();
    HasStateCombinerFlag = kqpProto.GetHasStateAggregation();
    GroupByKeysCount = kqpProto.GetGroupByKeysCount();
    HasFilterFlag = kqpProto.GetHasFilter();
    HasTopFlag = kqpProto.GetHasTop();
    HasRangeScanFlag = kqpProto.GetHasRangeScan();
    HasCondenseFlag = kqpProto.GetHasCondense();
    HasWatermarkGeneratorFlag = kqpProto.GetHasWatermarkGenerator();
    if (kqpProto.HasWatermarkGeneratorIdleTimeoutUs()) {
        WatermarkGeneratorIdleTimeoutUs = kqpProto.GetWatermarkGeneratorIdleTimeoutUs();
    } else {
        WatermarkGeneratorIdleTimeoutUs.reset();
    }
    NodesCount = kqpProto.GetNodesCount();
    InputDataPrediction = kqpProto.GetInputDataPrediction();
    OutputDataPrediction = kqpProto.GetOutputDataPrediction();
    StageLevel = kqpProto.GetStageLevel();
    LevelDataPrediction = kqpProto.GetLevelDataPrediction();
    WasmUdfModules_.clear();
    for (const auto& module : kqpProto.GetWasmUdfModules()) {
        WasmUdfModules_.insert(module);
    }
    WasmUdfStringColumns_.clear();
    for (const auto& column : kqpProto.GetWasmUdfStringColumns()) {
        WasmUdfStringColumns_.insert(column);
    }
    return true;
}

TVector<TString> TStagePredictor::GetWasmUdfModules() const {
    TVector<TString> modules(WasmUdfModules_.begin(), WasmUdfModules_.end());
    Sort(modules);
    return modules;
}

TVector<TString> TStagePredictor::GetWasmUdfStringColumns() const {
    TVector<TString> columns(WasmUdfStringColumns_.begin(), WasmUdfStringColumns_.end());
    Sort(columns);
    return columns;
}

ui32 TStagePredictor::GetUsableThreads() {
    std::optional<ui32> userPoolSize;
    if (HasAppData() && TlsActivationContext && TlsActivationContext->ActorSystem()) {
        userPoolSize = TlsActivationContext->ActorSystem()->GetPoolThreadsCount(AppData()->UserPoolId);
    }
    if (!userPoolSize) {
        YDB_LOG_INFO("User pool is undefined for executer tasks construction");
        userPoolSize = NSystemInfo::NumberOfCpus();
    }
    return Max<ui32>(1, *userPoolSize);
}

ui32 TStagePredictor::GetPossibleMaxLimitThreads() {
    const ui32 usableThreads = GetUsableThreads();
    if (HasAppData() && TlsActivationContext && TlsActivationContext->ActorSystem()) {
        TExecutorPoolState poolState;
        GetActorSystemStats().GetExecutorPoolState(AppData()->UserPoolId, poolState);
        if (poolState.PossibleMaxLimit > 0) {
            return Max(usableThreads, static_cast<ui32>(std::ceil(poolState.PossibleMaxLimit)));
        }
    }

    return usableThreads;
}

ui32 TStagePredictor::CalcTasksOptimalCount(const ui32 availableThreadsCount, const std::optional<ui32> previousStageTasksCount) const {
    ui32 result = 0;
    if (!LevelDataPrediction || *LevelDataPrediction == 0) {
        YDB_LOG_ERROR("Level difficulty not defined for correct calculation");
        result = availableThreadsCount;
    } else {
        result = (availableThreadsCount - previousStageTasksCount.value_or(0) * 0.25) * (InputDataPrediction / *LevelDataPrediction);
    }
    if (previousStageTasksCount && *previousStageTasksCount > 0) {
        result = std::min<ui32>(result, *previousStageTasksCount);
    }
    return std::max<ui32>(1, result);
}

bool TStagePredictor::NeedLLVM() const {
    return HasStateCombinerFlag || HasFinalCombinerFlag || HasCondenseFlag;
}

TStagePredictor& TRequestPredictor::BuildForStage(const NYql::NNodes::TDqPhyStage& stage, NYql::TExprContext& ctx) {
    StagePredictors.emplace_back();
    TStagePredictor& result = StagePredictors.back();
    StagesMap.emplace(stage.Ref().UniqueId(), &result);
    result.Scan(stage.Program().Ptr());

    for (ui32 inputIndex = 0; inputIndex < stage.Inputs().Size(); ++inputIndex) {
        const auto& input = stage.Inputs().Item(inputIndex);

        if (input.Maybe<NYql::NNodes::TDqSource>()) {
        } else {
            YQL_ENSURE(input.Maybe<NYql::NNodes::TDqConnection>());
            auto connection = input.Cast<NYql::NNodes::TDqConnection>();
            auto it = StagesMap.find(connection.Output().Stage().Ref().UniqueId());
            YQL_ENSURE(it != StagesMap.end(), "stage #" << connection.Output().Stage().Ref().UniqueId() << " not found in stages map for prediction: "
                << PrintKqpStageOnly(connection.Output().Stage(), ctx));
            result.AcceptInputStageInfo(*it->second, connection);
        }
    }
    result.Prepare();
    return result;
}

double TRequestPredictor::GetLevelDataVolume(const ui32 level) const {
    double result = 0;
    for (auto&& i : StagePredictors) {
        if (i.GetStageLevel() == level) {
            result += i.GetInputDataPrediction();
        }
    }
    return result;
}

}
