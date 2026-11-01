#include "shop/TradeRecord.h"

#include "shop/JsonLine.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace ss::shop {
namespace {

/// spec 目前只有这一种事件（以后加 `create` / `close` 时改这一处）。
constexpr char kEventTrade[] = "trade";

nlohmann::ordered_json shopObject(TradeRecord::ShopCoord const& shop) {
    nlohmann::ordered_json out;
    out["dim"] = shop.dimensionId;
    out["x"]   = shop.x;
    out["y"]   = shop.y;
    out["z"]   = shop.z;
    return out;
}

nlohmann::ordered_json partyObject(TradeRecord::Party const& party) {
    nlohmann::ordered_json out;
    out["xuid"] = party.xuid;
    out["name"] = party.name;
    return out;
}

nlohmann::ordered_json itemObject(TradeRecord::Item const& item) {
    nlohmann::ordered_json out;
    out["id"]   = item.id;
    out["aux"]  = item.aux;
    out["name"] = item.name;
    return out;
}

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
    // 历史流水不能跟着变。每加一个 `TradeRejection` 值都要同步加到这里（spec 明令）。
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
    record.gross     = plan.total;
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
    // `ok` 时是空串而不是省掉这个键：键集合固定，`duckdb` 的列推断与 `jq -c` 都省事。
    row["reason"]  = record.reason;
    row["dir"]     = record.dir == TradeDirection::Buy ? "buy" : "sell";
    row["shop"]    = shopObject(record.shop);
    row["owner"]   = partyObject(record.owner);
    row["admin"]   = record.admin;
    row["backend"] = record.backend;
    row["player"]  = partyObject(record.player);
    row["item"]    = itemObject(record.item);
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

std::string transactionLogFileName(std::string_view date) {
    return "transactions-" + std::string{date} + ".jsonl";
}

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
    return {};
}

} // namespace ss::shop
