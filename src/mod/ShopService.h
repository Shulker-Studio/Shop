#pragma once

#include "mod/Config.h"
#include "shop/ShopSign.h"

#include <string>
#include <string_view>

class BlockPos;
class BlockSource;
class ServerPlayer;

namespace ss::shop {

enum class SignEditOutcome {
    NotAShopSign, ///< 不像商店牌：包原样放行，绝不动玩家的牌子
    Rejected,     ///< 像商店但写错了：丢弃这个包，牌子保留原文本
    Normalized,   ///< 合法：把规范化后的四行写回包
};

struct SignEditResult {
    SignEditOutcome outcome = SignEditOutcome::NotAShopSign;
    std::string     message;        ///< Rejected 时发给玩家的原因
    std::string     normalizedText; ///< Normalized 时写回 `event.text()`
};

/// 解析错误 → 玩家提示文案（中文，面向服主与玩家）。
std::string messageFor(ShopSignError error, int maxShopAmount);

/// 判断一次告示牌正面编辑会把商店带到什么状态。
///
/// 只做决策：不写方块、不发消息、不取消事件——这些交给事件层。
SignEditResult planSignEdit(
    BlockSource&      region,
    BlockPos const&   signPos,
    ServerPlayer&     editor,
    std::string_view  rawText,
    ShopConfig const& config
);

} // namespace ss::shop
