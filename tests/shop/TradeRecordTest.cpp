#include "shop/TradeRecord.h"

#include <doctest/doctest.h>
#include <string>

namespace {

using namespace ss::shop;

constexpr char kTs[] = "2026-11-01T13:23:12+08:00";

/// 普通店、玩家买入：店主 Steve 卖 3 份钻石，一份 10 元、一份 64 件。
/// `gross`（主交易金额）不在这里填：它来自 `Payment::main`，只有真的动了钱才有意义，
/// 所以每个用例自己决定该是几。
TradeFacts buyFacts() {
    TradeFacts facts;
    facts.dir         = TradeDirection::Buy;
    facts.dimensionId = 0;
    facts.shop        = Cell{100, 64, -7};
    facts.ownerXuid   = "2535400000000001";
    facts.ownerName   = "Steve";
    facts.admin       = false;
    facts.backend     = "legacy_money";
    facts.playerXuid  = "2535400000000002";
    facts.playerName  = "Alex";
    facts.itemId      = "minecraft:diamond";
    facts.itemAux     = 0;
    facts.itemName    = "钻石";
    facts.unitPrice   = 10;
    return facts;
}

TradePlan threeUnits() { return TradePlan{.units = 3, .items = 192, .total = 30, .tax = 0}; }

} // namespace

TEST_CASE("交易流水：ok 一行，键序固定（jq 直读）") {
    // 键序是本票对外契约的一部分：
    // ts / event / result / dir / shop{dim,x,y,z} / owner{xuid,name} / admin / backend /
    // player{xuid,name} / item{id,aux,name} / units / items / unitPrice / gross / tax / total
    // （`reason` 只在失败/冻结的行上出现，见下一个用例）
    auto facts  = buyFacts();
    facts.gross = 30;
    CHECK(
        tradeRecordJson(composeTradeRecord(facts, threeUnits(), TradeLogResult::Ok, "", kTs))
        == R"({"ts":"2026-11-01T13:23:12+08:00","event":"trade","result":"ok","dir":"buy","shop":{"dim":0,"x":100,"y":64,"z":-7},"owner":{"xuid":"2535400000000001","name":"Steve"},"admin":false,"backend":"legacy_money","player":{"xuid":"2535400000000002","name":"Alex"},"item":{"id":"minecraft:diamond","aux":0,"name":"钻石"},"units":3,"items":192,"unitPrice":10,"gross":30,"tax":0,"total":30})"
           "\n"
    );
}

TEST_CASE("交易流水：fail 与 frozen 也各是一行，带机器可读原因（不是中文文案）") {
    auto facts = buyFacts();
    facts.dir  = TradeDirection::Sell;

    // `fail`：一份都没成交，所以份数与金额都是 0（而不是被省掉）。
    CHECK(
        tradeRecordJson(composeTradeRecord(facts, TradePlan{}, TradeLogResult::Fail, "no_money", kTs))
        == R"({"ts":"2026-11-01T13:23:12+08:00","event":"trade","result":"fail","reason":"no_money","dir":"sell","shop":{"dim":0,"x":100,"y":64,"z":-7},"owner":{"xuid":"2535400000000001","name":"Steve"},"admin":false,"backend":"legacy_money","player":{"xuid":"2535400000000002","name":"Alex"},"item":{"id":"minecraft:diamond","aux":0,"name":"钻石"},"units":0,"items":0,"unitPrice":10,"gross":0,"tax":0,"total":0})"
           "\n"
    );

    // `frozen`：补偿失败，键序与上一行完全相同，只有 `result` / `reason` 不同。
    CHECK(
        tradeRecordJson(composeTradeRecord(facts, TradePlan{}, TradeLogResult::Frozen, "compensation_failed", kTs))
        == R"({"ts":"2026-11-01T13:23:12+08:00","event":"trade","result":"frozen","reason":"compensation_failed","dir":"sell","shop":{"dim":0,"x":100,"y":64,"z":-7},"owner":{"xuid":"2535400000000001","name":"Steve"},"admin":false,"backend":"legacy_money","player":{"xuid":"2535400000000002","name":"Alex"},"item":{"id":"minecraft:diamond","aux":0,"name":"钻石"},"units":0,"items":0,"unitPrice":10,"gross":0,"tax":0,"total":0})"
           "\n"
    );
}

TEST_CASE("交易流水：管理员店强制 owner.xuid 为空、tax 为 0") {
    auto facts      = buyFacts();
    facts.admin     = true;
    facts.ownerName = "Admin Shop";
    facts.gross     = 10; // 管理员店没有税，主交易金额就是牌面额
    // 牌上仍然记着创建者的 XUID，方案里也可能算出税：两者都必须被清掉，
    // 否则「管理员店没有可收税的成交额、对手方不是玩家」这两条会被流水说成另一个样子。
    auto const record = composeTradeRecord(
        facts,
        TradePlan{.units = 1, .items = 64, .total = 10, .tax = 5},
        TradeLogResult::Ok,
        "",
        kTs
    );

    CHECK(record.owner.xuid.empty());
    CHECK(record.owner.name == "Admin Shop");
    CHECK(record.tax == 0);
    CHECK(record.admin);
    // 玩家付的就是牌面额
    CHECK(record.total == 10);
    CHECK(tradeRecordProblem(record).empty());
}

TEST_CASE("交易流水：含引号与换行的物品名不会把记录撕成两行") {
    auto facts       = buyFacts();
    facts.gross      = 30;
    facts.itemName   = "带\"引号\"和\\反斜杠的物品";
    facts.playerName = "第一行\n第二行";

    auto const json = tradeRecordJson(composeTradeRecord(facts, threeUnits(), TradeLogResult::Ok, "", kTs));

    // 恰好一个换行，而且在末尾：整条记录必须是一行（NDJSON 的硬要求）
    CHECK(json.find('\n') == json.size() - 1);
    CHECK(json.find("\\\"引号\\\"") != std::string::npos);
    CHECK(json.find("\\\\反斜杠") != std::string::npos);
    CHECK(json.find("\\n") != std::string::npos);
    CHECK(json.find('\r') == std::string::npos);
    CHECK(json.find("带") != std::string::npos); // 名字没有被整段吞掉
    CHECK(json.find("的物品") != std::string::npos);
}

TEST_CASE("交易流水：中文原样写（UTF-8 不转成 \\u）") {
    auto facts      = buyFacts();
    facts.gross     = 30;
    auto const json = tradeRecordJson(composeTradeRecord(facts, threeUnits(), TradeLogResult::Ok, "", kTs));
    CHECK(json.find("钻石") != std::string::npos);
    CHECK(json.find("\\u94bb") == std::string::npos);
}

TEST_CASE("交易流水：文件名按日期生成，跨天就是另一个文件") {
    // 文件名只留日期：类型由所在目录（`logs/transactions/`）区分，再带前缀就是重复信息。
    CHECK(transactionLogFileName("2026-11-01") == "2026-11-01.jsonl");
    CHECK(transactionLogFileName("2026-12-31") == "2026-12-31.jsonl");
}

TEST_CASE("交易流水：矛盾组合被拒（校验函数是落盘前的最后一道闸）") {
    auto paid  = buyFacts();
    paid.gross = 30;

    // 合规的那两条：一个都不报
    CHECK(tradeRecordProblem(composeTradeRecord(paid, threeUnits(), TradeLogResult::Ok, "", kTs)).empty());
    CHECK(
        tradeRecordProblem(composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Fail, "no_stock", kTs)).empty()
    );

    // ok 却带着原因：读流水的人会以为这笔生意失败过
    CHECK_FALSE(
        tradeRecordProblem(composeTradeRecord(paid, threeUnits(), TradeLogResult::Ok, "no_money", kTs)).empty()
    );
    // fail / frozen 不带原因：一行没有原因的失败，等于什么都没记
    CHECK_FALSE(tradeRecordProblem(composeTradeRecord(buyFacts(), TradePlan{}, TradeLogResult::Fail, "", kTs)).empty());
    CHECK_FALSE(
        tradeRecordProblem(composeTradeRecord(buyFacts(), TradePlan{}, TradeLogResult::Frozen, "", kTs)).empty()
    );

    // 手拼出来的矛盾记录（绕过 composeTradeRecord 的强制才能造出来）
    auto record = composeTradeRecord(paid, threeUnits(), TradeLogResult::Ok, "", kTs);
    CHECK_FALSE(record.owner.xuid.empty());
    record.admin = true;
    CHECK_FALSE(tradeRecordProblem(record).empty()); // admin 但 owner.xuid 不是空串
    record.owner.xuid.clear();
    record.tax = 7;
    CHECK_FALSE(tradeRecordProblem(record).empty()); // admin 但税不是 0

    // 主交易金额不可能比玩家实付/实收的总额还大（买入的税是**另外**一笔付给系统账户的）
    record       = composeTradeRecord(paid, threeUnits(), TradeLogResult::Ok, "", kTs);
    record.gross = record.total + 1;
    CHECK_FALSE(tradeRecordProblem(record).empty());

    // 没有时间戳的记录进不了流水：文件名要从它推出来
    CHECK_FALSE(tradeRecordProblem(composeTradeRecord(paid, threeUnits(), TradeLogResult::Ok, "", "")).empty());
}

TEST_CASE("交易流水：拒绝原因 → 机器可读短名（不把中文文案写进 reason）") {
    // spec：`reason` 用短名，文案会变而短名是查询键。每加一个 `TradeRejection` 都要同步加进这张表。
    CHECK(std::string{tradeRejectionReasonName(TradeRejection::ShopClosed)} == "shop_closed");
    CHECK(std::string{tradeRejectionReasonName(TradeRejection::OwnerOffline)} == "owner_offline");
    CHECK(std::string{tradeRejectionReasonName(TradeRejection::NotEnoughGoods)} == "no_stock");
    CHECK(std::string{tradeRejectionReasonName(TradeRejection::NotEnoughFunds)} == "no_money");
    CHECK(std::string{tradeRejectionReasonName(TradeRejection::NoRoomToReceive)} == "no_space");
    CHECK(std::string{tradeRejectionReasonName(TradeRejection::OverUnitLimit)} == "over_limit");
    CHECK(std::string{tradeRejectionReasonName(TradeRejection::InvalidRequest)} == "invalid_request");
    // `None` 不是「拒绝」：它没有短名，`ok` 那一行的 reason 就是它
    CHECK(tradeRejectionReasonName(TradeRejection::None).empty());
}

TEST_CASE("交易流水：frozen_pair 记的是「这一对已被冻结」，不是又一条 frozen") {
    // 同一对「玩家 + 商店」在补偿失败之后被钉住，后续提交是被拒（fail）而不是又坏了一次（frozen）：
    // 记成 frozen 会让服主以为事故发生了两次。
    auto const record = composeTradeRecord(buyFacts(), TradePlan{}, TradeLogResult::Fail, "frozen_pair", kTs);
    CHECK(tradeRecordProblem(record).empty());
    CHECK(record.result == TradeLogResult::Fail);
    CHECK(record.reason == "frozen_pair");
    auto const json = tradeRecordJson(record);
    CHECK(json.find(R"("reason":"frozen_pair")") != std::string::npos);
    CHECK(json.find(R"("reason":"compensation_failed")") == std::string::npos);
}

TEST_CASE("交易流水：免费店（0 元成交）照样是一行 ok") {
    // 「0 元的成交」是有意义的历史：它证明那次交接发生过（管理员店的期限价、免费领取）。
    auto facts        = buyFacts();
    facts.gross       = 0; // 一分钱都没动过，所以主交易金额也是 0
    auto const record = composeTradeRecord(
        facts,
        TradePlan{.units = 2, .items = 128, .total = 0, .tax = 0},
        TradeLogResult::Ok,
        "",
        kTs
    );
    CHECK(record.gross == 0);
    CHECK(record.total == 0);
    CHECK(tradeRecordProblem(record).empty());
    CHECK(tradeRecordJson(record).find(R"("gross":0,"tax":0,"total":0)") != std::string::npos);
}

TEST_CASE("交易流水：三态的名字就是文件里那三个词") {
    // 包成 `std::string` 再比：doctest 打印 `std::string_view` 需要额外的 `<ostream>`，
    // 而这条断言要的就是那三个词的**文本**，包一层正好也是想断言的东西。
    CHECK(std::string{tradeLogResultName(TradeLogResult::Ok)} == "ok");
    CHECK(std::string{tradeLogResultName(TradeLogResult::Fail)} == "fail");
    CHECK(std::string{tradeLogResultName(TradeLogResult::Frozen)} == "frozen");
}
