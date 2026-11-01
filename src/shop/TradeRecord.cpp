#include "shop/TradeRecord.h"

#include "shop/JsonLine.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace ss::shop {
namespace {

/// spec 目前只有这一种事件（以后加 `create` / `close` 时改这一处）。
constexpr char kEventTrade[] = "trade";

} // namespace

std::string_view tradeLogResultName(TradeLogResult result) {
    switch (result) {
    case TradeLogResult::Fail:
        return "fail";
    case TradeLogResult::Frozen:
        return "frozen";
    case TradeLogResult::Ok:
    default:
        return "ok";
    }
}

std::string_view tradeRejectionReasonName(TradeRejection rejection) {
    // 短名是**查询键**：英文小写下划线、不含中文。文案（`tradeRejectionMessage`）随时会改，
    // 历史流水不能跟着变。每加一个 `TradeRejection` 值都要同步加到这里（spec 明令）——
    // **只改这一处**：mod 侧的 `RejectReason` 是另一套互不重叠的键（世界状态，见那里），
    // 不是这张表的分身。
    switch (rejection) {
    case TradeRejection::ShopClosed:
        return "shop_closed";
    case TradeRejection::OwnerOffline:
        return "owner_offline";
    case TradeRejection::NotEnoughGoods:
        return "no_stock";
    case TradeRejection::NotEnoughFunds:
        return "no_money";
    case TradeRejection::NoRoomToReceive:
        return "no_space";
    case TradeRejection::OverUnitLimit:
        return "over_limit";
    case TradeRejection::InvalidRequest:
        return "invalid_request";
    case TradeRejection::None:
    default:
        // `None` 不是拒绝：`ok` 那一行的 `reason` 就是它（空串）。
        return {};
    }
}

TradeRecord composeTradeRecord(
    TradeFacts const& facts,
    TradePlan const&  plan,
    TradeLogResult    result,
    std::string_view  reason,
    std::string       ts
) {
    TradeRecord record;
    record.ts     = std::move(ts);
    record.result = result;
    record.reason = std::string{reason};
    record.dir    = facts.dir;
    record.shop   = TradeRecord::ShopCoord{facts.dimensionId, facts.shop.x, facts.shop.y, facts.shop.z};
    record.admin  = facts.admin;
    // 管理员店的对手方是系统账户（DESIGN §7.4）：牌上那个 XUID 只是当时的创建者，
    // 不是能收钱的店主。清空它而不是留着，免得流水让人去追一个不存在的账户。
    record.owner   = TradeRecord::Party{facts.admin ? std::string{} : facts.ownerXuid, facts.ownerName};
    record.backend = facts.backend;
    record.player  = TradeRecord::Party{facts.playerXuid, facts.playerName};
    record.item    = TradeRecord::Item{facts.itemId, facts.itemAux, facts.itemName};

    record.units     = plan.units;
    record.items     = plan.items;
    record.unitPrice = facts.unitPrice;
    // `gross` 是**主交易金额**（钱真正在玩家与店那一侧之间移动的那一笔 = `Payment::main`），
    // 由调用方填；`total` 是玩家实际付出/收到的总额。买入方向带税时两者**不相等**
    // （买家的税是另一笔付给系统账户的），所以两个字段都有存在的理由，不是重复。
    record.gross = facts.gross;
    // 管理员店的成交额本身就是销毁 / 凭空记账，没有可抽税的地方（`tradeTaxPlan` 也已经给了 0）。
    record.tax   = facts.admin ? 0 : plan.tax;
    record.total = plan.total;
    return record;
}

std::string tradeRecordJson(TradeRecord const& record) {
    nlohmann::ordered_json row;
    row["ts"]     = record.ts;
    row["event"]  = kEventTrade;
    row["result"] = std::string{tradeLogResultName(record.result)};
    // `ok` **不写** `reason` 这个键（spec 的字段表：`reason` 只在 `result != ok` 时有意义）。
    // 写一个空串也「能查」，但 `jq 'has("reason")'` 就再也分不出成交与失败的行——
    // 而那道区分正是这个字段存在的理由。
    if (!record.reason.empty()) {
        row["reason"] = record.reason;
    }
    row["dir"]       = record.dir == TradeDirection::Buy ? "buy" : "sell";
    row["shop"]      = jsonShop(record.shop.dimensionId, record.shop.x, record.shop.y, record.shop.z);
    row["owner"]     = jsonParty(record.owner.xuid, record.owner.name);
    row["admin"]     = record.admin;
    row["backend"]   = record.backend;
    row["player"]    = jsonParty(record.player.xuid, record.player.name);
    row["item"]      = jsonItem(record.item.id, record.item.aux, record.item.name);
    row["units"]     = record.units;
    row["items"]     = record.items;
    row["unitPrice"] = record.unitPrice;
    row["gross"]     = record.gross;
    row["tax"]       = record.tax;
    row["total"]     = record.total;

    // `ensure_ascii = false`：中文原样写。非法 UTF-8 换成 U+FFFD 而不是抛异常——
    // 本工程 `set_exceptions("none")`，一条坏名字不值得让整个进程 abort。
    return jsonLine(row);
}

std::string transactionLogFileName(std::string_view date) { return "transactions-" + std::string{date} + ".jsonl"; }

std::string_view tradeRecordProblem(TradeRecord const& record) {
    if (record.ts.empty()) {
        return "缺少 ts：文件名要从它推出来";
    }
    if (record.result == TradeLogResult::Ok && !record.reason.empty()) {
        return "result 是 ok 却带着 reason";
    }
    if (record.result != TradeLogResult::Ok && record.reason.empty()) {
        return "result 不是 ok 却没有 reason";
    }
    if (record.admin && !record.owner.xuid.empty()) {
        return "管理员店的 owner.xuid 必须是空串";
    }
    if (record.admin && record.tax != 0) {
        return "管理员店的 tax 必须是 0";
    }
    if (record.gross > record.total) {
        // 主交易金额只会**小于或等于**玩家侧的总额：买入的税是另外一笔付给系统账户的。
        // 大过总额说明 gross 不是从 `Payment::main` 来的，而那一行会让人以为账对不上。
        return "gross（主交易金额）不能大于 total";
    }
    return {};
}

} // namespace ss::shop
