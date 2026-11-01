#include "shop/TradeRecord.h"

#include <doctest/doctest.h>
#include <string>

namespace {

using namespace ss::shop;

constexpr char kTs[] = "2026-11-01T13:23:12+08:00";

/// 普通店、玩家买入：店主 Steve 卖 3 份钻石，一份 10 元、一份 64 件。
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
    // ts / event / result / reason / dir / shop{dim,x,y,z} / owner{xuid,name} / admin / backend /
    // player{xuid,name} / item{id,aux,name} / units / items / unitPrice / gross / tax / total
    CHECK(
        tradeRecordJson(composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Ok, "", kTs))
        == R"({"ts":"2026-11-01T13:23:12+08:00","event":"trade","result":"ok","reason":"","dir":"buy","shop":{"dim":0,"x":100,"y":64,"z":-7},"owner":{"xuid":"2535400000000001","name":"Steve"},"admin":false,"backend":"legacy_money","player":{"xuid":"2535400000000002","name":"Alex"},"item":{"id":"minecraft:diamond","aux":0,"name":"钻石"},"units":3,"items":192,"unitPrice":10,"gross":30,"tax":0,"total":30})"
               "\n"
    );
}

TEST_CASE("交易流水：fail 与 frozen 都带机器可读原因（不是中文文案）") {
    auto facts        = buyFacts();
    facts.dir         = TradeDirection::Sell;
    auto const failed = tradeRecordJson(composeTradeRecord(facts, TradePlan{}, TradeLogResult::Fail, "no_money", kTs));
    CHECK(failed.find(R"("result":"fail")") != std::string::npos);
    CHECK(failed.find(R"("reason":"no_money")") != std::string::npos);
    CHECK(failed.find(R"("dir":"sell")") != std::string::npos);
    // 一份都没成交：数字是 0，而不是被省掉
    CHECK(failed.find(R"("units":0,"items":0,"unitPrice":10,"gross":0,"tax":0,"total":0)") != std::string::npos);

    auto const frozen = tradeRecordJson(
        composeTradeRecord(facts, TradePlan{}, TradeLogResult::Frozen, "compensation_failed", kTs)
    );
    CHECK(frozen.find(R"("result":"frozen")") != std::string::npos);
    CHECK(frozen.find(R"("reason":"compensation_failed")") != std::string::npos);
}

TEST_CASE("交易流水：管理员店强制 owner.xuid 为空、tax 为 0") {
    auto facts      = buyFacts();
    facts.admin     = true;
    facts.ownerName = "Admin Shop";
    // 牌上仍然记着创建者的 XUID，方案里也可能算出税：两者都必须被清掉，
    // 否则「管理员店没有可收税的成交额、对手方不是玩家」这两条会被流水说成另一个样子。
    auto const record =
        composeTradeRecord(facts, TradePlan{.units = 1, .items = 64, .total = 10, .tax = 5}, TradeLogResult::Ok, "", kTs);

    CHECK(record.owner.xuid.empty());
    CHECK(record.owner.name == "Admin Shop");
    CHECK(record.tax == 0);
    CHECK(record.admin);
    // 成交额仍按牌面记：税是店侧的账，玩家付的就是牌面额
    CHECK(record.gross == 10);
    CHECK(record.total == 10);
    CHECK(tradeRecordProblem(record).empty());
}

TEST_CASE("交易流水：含引号与换行的物品名不会把记录撕成两行") {
    auto facts      = buyFacts();
    facts.itemName  = "带\"引号\"和\\反斜杠的物品";
    facts.playerName = "第一行\n第二行";

    auto const json = tradeRecordJson(composeTradeRecord(facts, threeUnits(), TradeLogResult::Ok, "", kTs));

    // 恰好一个换行，而且在末尾：整条记录必须是一行（NDJSON 的硬要求）
    CHECK(json.find('\n') == json.size() - 1);
    CHECK(json.find("\\\"引号\\\"") != std::string::npos);
    CHECK(json.find("\\\\反斜杠") != std::string::npos);
    CHECK(json.find("\\n") != std::string::npos);
    CHECK(json.find('\r') == std::string::npos);
    CHECK(json.find("带") != std::string::npos);          // 名字没有被整段吞掉
    CHECK(json.find("的物品") != std::string::npos);
}

TEST_CASE("交易流水：中文原样写（UTF-8 不转成 \\u）") {
    auto const json = tradeRecordJson(composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Ok, "", kTs));
    CHECK(json.find("钻石") != std::string::npos);
    CHECK(json.find("\\u94bb") == std::string::npos);
}

TEST_CASE("交易流水：文件名按日期生成，跨天就是另一个文件") {
    CHECK(transactionLogFileName("2026-11-01") == "transactions-2026-11-01.jsonl");
    CHECK(transactionLogFileName("2026-12-31") == "transactions-2026-12-31.jsonl");
    // 日期直接取 ts 的前 10 个字符，所以两者永远同一天
    CHECK(transactionLogFileName(std::string{kTs}.substr(0, 10)) == "transactions-2026-11-01.jsonl");
}

TEST_CASE("交易流水：矛盾组合被拒（校验函数是落盘前的最后一道闸）") {
    // 合规的那一条：一个都不报
    CHECK(tradeRecordProblem(composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Ok, "", kTs)).empty());
    CHECK(
        tradeRecordProblem(composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Fail, "no_stock", kTs)).empty()
    );

    // ok 却带着原因：读流水的人会以为这笔生意失败过
    CHECK_FALSE(tradeRecordProblem(composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Ok, "no_money", kTs)).empty());
    // fail / frozen 不带原因：一行没有原因的失败，等于什么都没记
    CHECK_FALSE(tradeRecordProblem(composeTradeRecord(buyFacts(), TradePlan{}, TradeLogResult::Fail, "", kTs)).empty());
    CHECK_FALSE(tradeRecordProblem(composeTradeRecord(buyFacts(), TradePlan{}, TradeLogResult::Frozen, "", kTs)).empty());

    // 手拼出来的矛盾记录（绕过 composeTradeRecord 才能造出来）：管理员店的税与店主账户
    auto record = composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Ok, "", kTs);
    CHECK(record.owner.xuid.empty() == false);
    record.admin = true;
    CHECK_FALSE(tradeRecordProblem(record).empty()); // admin 但 owner.xuid 不是空串
    record.owner.xuid.clear();
    record.tax = 7;
    CHECK_FALSE(tradeRecordProblem(record).empty()); // admin 但税不是 0

    // 没有时间戳的记录进不了流水：文件名要从它推出来
    record = composeTradeRecord(buyFacts(), threeUnits(), TradeLogResult::Ok, "", "");
    CHECK_FALSE(tradeRecordProblem(record).empty());
}

TEST_CASE("交易流水：三态的名字就是文件里那三个词") {
    // 包成 `std::string` 再比：doctest 打印 `std::string_view` 需要额外的 `<ostream>`，
    // 而这条断言要的就是那三个词的**文本**，包一层正好也是想断言的东西。
    CHECK(std::string{tradeLogResultName(TradeLogResult::Ok)} == "ok");
    CHECK(std::string{tradeLogResultName(TradeLogResult::Fail)} == "fail");
    CHECK(std::string{tradeLogResultName(TradeLogResult::Frozen)} == "frozen");
}
