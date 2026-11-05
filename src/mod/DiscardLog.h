#pragma once

#include "shop/ContainerFinder.h"
#include "shop/DiscardRecord.h"

#include "mc/world/item/ItemStack.h"

#include <string>
#include <vector>

class Player;

namespace ss::shop {

/// 把一堆**已经被取出来、即将销毁**的物品描述成留档记录（`p1-transaction-log` spec D5）。
///
/// 一个物品堆一条记录：一次卖出 3 组就是 3 条，共用同一个 `ts` 与 `shop`。
/// SNBT 在这里生成（`ItemStack::save(SaveContext{Clone})->toString()`）——它是本次交易里
/// 唯一一次读到被销毁物品的机会，记录必须在 `Done` 之前造出来。
///
/// 不写文件：落盘是 `writeDiscardRecords` 的事，而且只在交易**真的成交**之后才发生。
std::vector<DiscardRecord> describeDiscardedItems(
    std::string const&            ts,
    int                           dimensionId,
    Cell const&                   sign,
    Player&                       player,
    std::vector<ItemStack> const& stacks
);

/// 追加写到 `mods/Shop/logs/discarded-YYYY-MM-DD.jsonl`。
///
/// 失败只 `warn`：走到这里交易**已经成交**，留档写不进去也不能把玩家那笔生意判成失败
/// （spec D5 / 票 02）。同一批记录共用同一个 `ts`，所以文件名取第一条的日期；
/// 这一批横跨零点的那几毫秒不值得为此引入一个 map。
void writeDiscardRecords(std::vector<DiscardRecord> const& records);

} // namespace ss::shop
