#include "shop/DiscardRecord.h"

#include "shop/JsonLine.h"

namespace ss::shop {
namespace {

nlohmann::ordered_json shopObject(DiscardRecord const& record) {
    nlohmann::ordered_json out;
    out["dim"] = record.dimensionId;
    out["x"]   = record.x;
    out["y"]   = record.y;
    out["z"]   = record.z;
    return out;
}

nlohmann::ordered_json playerObject(DiscardRecord const& record) {
    nlohmann::ordered_json out;
    out["xuid"] = record.playerXuid;
    out["name"] = record.playerName;
    return out;
}

nlohmann::ordered_json itemObject(DiscardRecord const& record) {
    nlohmann::ordered_json out;
    out["id"]   = record.item.id;
    out["aux"]  = record.item.aux;
    out["name"] = record.item.name;
    // 完整 SNBT 原样透传（可能几十 KB）；转义与结尾由 `jsonLine` 统一负责。
    out["nbt"] = record.item.nbt;
    return out;
}

} // namespace

std::string discardRecordJson(DiscardRecord const& record) {
    // 键序是给人和 `jq` 读的契约：调试时「第 4 个字段是物品」比「先 jq 出 keys」省事。
    nlohmann::ordered_json row;
    row["ts"]     = record.ts;
    row["shop"]   = shopObject(record);
    row["player"] = playerObject(record);
    row["item"]   = itemObject(record);
    return jsonLine(row);
}

std::string discardLogFileName(std::string_view date) {
    return "discarded-" + std::string{date} + ".jsonl";
}

} // namespace ss::shop
