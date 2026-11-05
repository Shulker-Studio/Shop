#pragma once

#include "core/shop/ShopInfoData.h"

#include <optional>
#include <string>

class BlockPos;
class BlockSource;
namespace ss::shop {

struct ShopConfig;


/// 点牌时的一次性快照：解析结果 + 店主账户 + 可成交份数。
struct ShopSnapshot {
    ShopInfoData info;
    /// 店主账户键（XUID，ADR-0005）。读不到时为空，交易时就不能成交（没人能收款）。
    std::string ownerXuid;
};

/// 从世界状态读一块商店牌。
///
/// 返回空 = 这块牌现在不是可交易的商店：正面不是合法商店牌、或附着格不是可用容器。
/// 容器空了（卖光了）仍然返回快照——牌还是商店，只是 0 份。
///
/// 不看是谁在看、也不读谁的余额：余额是「谁在看」的属性，由 `inspectShop` 填。
/// 只读取：不写方块、不发消息。
std::optional<ShopSnapshot> readShop(BlockSource& region, BlockPos const& signPos, ShopConfig const& config);

} // namespace ss::shop
