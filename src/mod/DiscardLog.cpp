#include "mod/DiscardLog.h"

#include "mod/LogFile.h"
#include "mod/Shop.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/actor/player/Player.h"

#include <string>
#include <utility>
#include <vector>

namespace ss::shop {

std::vector<DiscardRecord> describeDiscardedItems(
    std::string const&            ts,
    int                           dimensionId,
    Cell const&                   sign,
    Player&                       player,
    std::vector<ItemStack> const& stacks
) {
    std::vector<DiscardRecord> records;
    records.reserve(stacks.size());

    for (auto const& stack : stacks) {
        if (stack.isNull()) {
            continue; // 空堆不是「被销毁的东西」，写进去只会是一个没有意义的空记录
        }

        // 完整 SNBT：`ItemStack` 自己没有 `save(CompoundTag&)`，只有这个返回新 `CompoundTag` 的重载。
        auto const tag = stack.save(SaveContext{SaveContext::SaveUseCase::Clone});

        DiscardRecord record;
        record.ts          = ts;
        record.dimensionId = dimensionId;
        record.x           = sign.x;
        record.y           = sign.y;
        record.z           = sign.z;
        record.playerXuid  = player.getXuid();
        record.playerName  = player.getNameTag();
        record.item.id     = stack.getTypeName();
        record.item.aux    = static_cast<int>(stack.getAuxValue());
        record.item.name   = stack.getName();
        record.item.nbt    = tag != nullptr ? tag->toString() : std::string{};
        records.push_back(std::move(record));
    }
    return records;
}

void writeDiscardRecords(std::vector<DiscardRecord> const& records) {
    if (records.empty()) {
        return; // 普通店的卖出、两个方向的买入都不会产生留档：没有东西被销毁
    }

    // 与业务流水共用 `appendLogLines`（建目录 / 追加 / flush / 关 / 失败只 warn 一次）：
    // 两个文件的落盘策略只能是**一套**，否则「交易照常成功」这条保证会在其中一个上失守。
    // 文件名取第一条的日期——同一批记录共用同一个 `ts`，横跨零点的那几毫秒不值得
    // 为此引入一个 map。
    std::vector<std::string> lines;
    lines.reserve(records.size());
    for (auto const& record : records) {
        lines.push_back(discardRecordJson(record));
    }
    appendLogLines(logDirectory(), discardLogFileName(records.front().ts.substr(0, 10)), lines);
}

} // namespace ss::shop
