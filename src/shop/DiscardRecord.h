#pragma once

#include <string>
#include <string_view>

namespace ss::shop {

/// 一条「被销毁的物品堆」留档（`p1-transaction-log` 的 spec D5）。
///
/// 管理员店卖出方向收到的货是**销毁**的（`p1-admin-unlimited-stock` 的 spec D3）：箱子只是模板，
/// 收进来的东西不写进任何格子。销毁不可逆，所以每一条都在落盘之前先描述成这样一份纯数据——
/// 落盘归流水功能（`src/mod/DiscardLog`），这里只管「长什么样」。
///
/// 一行一条**物品堆**，不是一笔交易一行：一次卖出 3 组就是 3 行，共用同一个 `ts` 与 `shop`。
struct DiscardRecord {
    struct Item {
        std::string id;        ///< `ItemStack::getTypeName()`
        int         aux  = 0;  ///< `ItemStack::getAuxValue()`
        std::string name;      ///< 当时的展示名，方便人眼读
        std::string nbt;       ///< 完整 SNBT，原样透传（可能几十 KB，所以不进业务流水）
    };

    std::string ts;             ///< 服务器本地时间，ISO-8601 带时区偏移
    int         dimensionId = 0;
    int         x           = 0; ///< 商店牌坐标（不是容器坐标）
    int         y           = 0;
    int         z           = 0;
    std::string playerXuid;
    std::string playerName;
    Item        item;
};

/// 一行 JSON（NDJSON：键序固定、末尾一个 `\n`）：
/// `ts` / `shop{dim,x,y,z}` / `player{xuid,name}` / `item{id,aux,name,nbt}`。
///
/// 键序是给人和 `jq` 读的契约：调试时「第 4 个字段是物品」比「先 jq 出 keys」省事。
/// 字符串里的引号、反斜杠与控制字符都要转义——把一条记录撕成两行就不再是合法 NDJSON 了。
std::string discardRecordJson(DiscardRecord const& record);

/// `YYYY-MM-DD.jsonl`。`date` 就是 `ts` 的前 10 个字符，两者因此永远同一天。
///
/// 与业务流水同一套命名：类型由目录（`logs/discarded/`）区分，文件名只留日期。
std::string discardLogFileName(std::string_view date);

} // namespace ss::shop
