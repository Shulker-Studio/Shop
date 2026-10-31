#pragma once

#include "mod/Config.h"
#include "shop/ShopInfo.h"
#include "shop/ShopSign.h"

#include <optional>
#include <string>
#include <string_view>

class BlockPos;
class BlockSource;
class Player;
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
/// 不看是谁在看、也不读谁的余额：余额是「谁在着」的属性，由 `inspectShop` 填。
/// 只读取：不写方块、不发消息。
std::optional<ShopSnapshot> readShop(BlockSource& region, BlockPos const& signPos, ShopConfig const& config);

/// 一次交互该给谁看什么：快照 + 角色。
struct ShopInteraction {
    ShopSnapshot shop;
    /// 这个人是不是以管理身份在看（店主或 OP）——决定表单给管理入口还是买卖按钮。
    ShopFormRole role = ShopFormRole::Visitor;
};

/// 看待一块商店牌（点牌路径）。
///
/// 返回空 = 现在不该接管这次交互：看的是背面，或者这块牌不是商店。
/// 与 `readShop` 的区别只有两件：看视角、读 viewer 的余额、定角色。
///
/// 只读取：不写方块、不发消息。
std::optional<ShopInteraction>
inspectShop(BlockSource& region, BlockPos const& signPos, Player& viewer, ShopConfig const& config);

} // namespace ss::shop
