#include <ydb/core/kqp/query_data/kqp_predictor.h>
#include <ydb/core/kqp/expr_nodes/kqp_expr_nodes.h>

#include <yql/essentials/ast/yql_expr.h>
#include <yql/essentials/core/expr_nodes/yql_expr_nodes.h>
#include <yql/essentials/core/yql_type_annotation.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace NKikimr::NKqp;
using namespace NYql;
using namespace NYql::NNodes;

namespace {

const TDataExprType* StringType(TExprContext& ctx) {
    return ctx.MakeType<TDataExprType>(NUdf::EDataSlot::String);
}

void SetStringAnn(const TExprNode::TPtr& node, TExprContext& ctx) {
    node->SetTypeAnn(StringType(ctx));
}

TExprNode::TPtr MakeUdf(TExprContext& ctx, TPositionHandle pos) {
    return ctx.Builder(pos)
        .Callable("Udf")
            .Atom(0, "WasmMod.Func")
        .Seal()
        .Build();
}

TExprNode::TPtr MakeMember(TExprContext& ctx, TPositionHandle pos, const TExprNode::TPtr& structNode, TStringBuf name) {
    auto member = ctx.Builder(pos)
        .Callable("Member")
            .Add(0, structNode)
            .Atom(1, name)
        .Seal()
        .Build();
    SetStringAnn(member, ctx);
    return member;
}

} // namespace

Y_UNIT_TEST_SUITE(TStagePredictorWasmUdfStringColumns) {

Y_UNIT_TEST(MemberArgFromApplyUdf) {
    TExprContext ctx;
    const auto pos = ctx.AppendPosition({});

    auto row = ctx.NewArgument(pos, "row");
    auto blobMember = MakeMember(ctx, pos, row, "blob");
    auto otherMember = MakeMember(ctx, pos, row, "other"); // not passed to UDF

    auto apply = ctx.Builder(pos)
        .Callable("Apply")
            .Add(0, MakeUdf(ctx, pos))
            .Add(1, blobMember)
        .Seal()
        .Build();

    // Stage root keeps unused Member so VisitExpr sees it, but predictor must ignore it.
    auto root = ctx.Builder(pos)
        .List()
            .Add(0, apply)
            .Add(1, otherMember)
        .Seal()
        .Build();

    TStagePredictor predictor;
    predictor.Scan(root);

    const auto columns = predictor.GetWasmUdfStringColumns();
    UNIT_ASSERT_VALUES_EQUAL(columns.size(), 1u);
    UNIT_ASSERT_VALUES_EQUAL(columns[0], "blob");
}

Y_UNIT_TEST(WideMapArgumentMappedToColumns) {
    TExprContext ctx;
    const auto pos = ctx.AppendPosition({});

    // Minimal KqpWideReadTable with Columns = ['blob', 'other'] at child index 2.
    auto read = ctx.Builder(pos)
        .Callable("KqpWideReadTable")
            .Atom(0, "table")
            .Atom(1, "range")
            .List(2)
                .Atom(0, "blob")
                .Atom(1, "other")
            .Seal()
            .List(3)
            .Seal()
        .Seal()
        .Build();

    auto wideMap = ctx.Builder(pos)
        .Callable("WideMap")
            .Add(0, read)
            .Lambda(1)
                .Param("blobArg")
                .Param("otherArg")
                .Callable("Apply")
                    .Add(0, MakeUdf(ctx, pos))
                    .Arg(1, "blobArg")
                .Seal()
            .Seal()
        .Seal()
        .Build();

    // Type-annotate lambda args used as string UDF inputs.
    auto lambda = TCoWideMap(wideMap).Lambda();
    SetStringAnn(lambda.Args().Arg(0).Ptr(), ctx);
    SetStringAnn(lambda.Args().Arg(1).Ptr(), ctx);

    TStagePredictor predictor;
    predictor.Scan(wideMap);

    const auto columns = predictor.GetWasmUdfStringColumns();
    UNIT_ASSERT_VALUES_EQUAL(columns.size(), 1u);
    UNIT_ASSERT_VALUES_EQUAL(columns[0], "blob");
}

Y_UNIT_TEST(WideMapOverExpandMapArgumentMappedToColumns) {
    TExprContext ctx;
    const auto pos = ctx.AppendPosition({});

    // Physical shape used with DqSource: WideMap(ExpandMap(..., Member names), λ → Apply).
    auto expandMap = ctx.Builder(pos)
        .Callable("ExpandMap")
            .Callable(0, "ToFlow")
                .Atom(0, "src")
            .Seal()
            .Lambda(1)
                .Param("row")
                .Callable(0, "Member")
                    .Arg(0, "row")
                    .Atom(1, "blob")
                .Seal()
                .Callable(1, "Member")
                    .Arg(0, "row")
                    .Atom(1, "other")
                .Seal()
            .Seal()
        .Seal()
        .Build();

    auto wideMap = ctx.Builder(pos)
        .Callable("WideMap")
            .Add(0, expandMap)
            .Lambda(1)
                .Param("blobArg")
                .Param("otherArg")
                .Callable("Apply")
                    .Add(0, MakeUdf(ctx, pos))
                    .Arg(1, "blobArg")
                .Seal()
            .Seal()
        .Seal()
        .Build();

    // No type annotations — predictor must still resolve ExpandMap Member names.
    TStagePredictor predictor;
    predictor.Scan(wideMap);

    const auto columns = predictor.GetWasmUdfStringColumns();
    UNIT_ASSERT_VALUES_EQUAL(columns.size(), 1u);
    UNIT_ASSERT_VALUES_EQUAL(columns[0], "blob");
}

Y_UNIT_TEST(SerializeDeserializeRoundTrip) {
    TExprContext ctx;
    const auto pos = ctx.AppendPosition({});

    auto row = ctx.NewArgument(pos, "row");
    auto apply = ctx.Builder(pos)
        .Callable("Apply")
            .Add(0, MakeUdf(ctx, pos))
            .Add(1, MakeMember(ctx, pos, row, "payload"))
        .Seal()
        .Build();

    TStagePredictor predictor;
    predictor.Scan(apply);

    NYql::NDqProto::TProgram::TSettings settings;
    predictor.SerializeToKqpSettings(settings);
    UNIT_ASSERT_VALUES_EQUAL(settings.WasmUdfStringColumnsSize(), 1);
    UNIT_ASSERT_VALUES_EQUAL(settings.GetWasmUdfStringColumns(0), "payload");

    TStagePredictor restored;
    UNIT_ASSERT(restored.DeserializeFromKqpSettings(settings));
    UNIT_ASSERT_VALUES_EQUAL(restored.GetWasmUdfStringColumns(), predictor.GetWasmUdfStringColumns());
}

} // Y_UNIT_TEST_SUITE
