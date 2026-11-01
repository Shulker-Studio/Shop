#include "shop/DiscardRecord.h"

#include "shop/JsonLine.h"

namespace ss::shop {

std::string discardRecordJson(DiscardRecord const& record) {
    // 键序是给人和 `jq` 读的契约：调试时「第 4 个字段是物品」比「先 jq 出 keys」省事。
    // `shop` / `player` / `item` 三个子对象与业务流水共用 `jsonShop` / `jsonParty` / `jsonItem`
    // （`JsonLine.h`）：两个文件描述的是同一种世界，形状不该各写一份。
    nlohmann::ordered_json row;
    row["ts"]     = record.ts;
    row["shop"]   = jsonShop(record.dimensionId, record.x, record.y, record.z);
    row["player"] = jsonParty(record.playerXuid, record.playerName);
    // 完整 SNBT 原样透传（可能几十 KB），只有留档有这个键——业务流水里塞它会一行撑成几十 KB。
    auto item   = jsonItem(record.item.id, record.item.aux, record.item.name);
    item["nbt"] = record.item.nbt;
    row["item"] = item;
    return jsonLine(row);
}

std::string discardLogFileName(std::string_view date) { return "discarded-" + std::string{date} + ".jsonl"; }

} // namespace ss::shop
