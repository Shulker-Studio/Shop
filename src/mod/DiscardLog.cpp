#include "mod/DiscardLog.h"

#include "mod/Shop.h"

#include "ll/api/io/Logger.h"

#include "mc/deps/nbt/CompoundTag.h"
#include "mc/world/item/SaveContext.h"
#include "mc/world/actor/player/Player.h"

#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

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

    auto& logger = Shop::getInstance().getSelf().getLogger();
    auto  dir    = Shop::getInstance().getSelf().getConfigDir() / "logs";

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        logger.warn("销毁留档的目录建不出来（{}）：{}；交易已成交，这些记录丢了", dir.string(), ec.message());
        return;
    }

    auto const path = dir / discardLogFileName(records.front().ts.substr(0, 10));

    // `binary`：NDJSON 要求 `\n` 结尾，不能让文本模式把它翻成 `\r\n`。
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out) {
        logger.warn("销毁留档打不开（{}）；交易已成交，这些记录丢了", path.string());
        return;
    }
    for (auto const& record : records) {
        out << discardRecordJson(record);
    }
    out.flush();
    if (!out) {
        // 磁盘满、盘被拔掉都会走到这里。留一条痕迹，但**不**回头改交易结论。
        logger.warn("销毁留档写入失败（{}）；交易已成交，这些记录可能不完整", path.string());
    }
}

} // namespace ss::shop
