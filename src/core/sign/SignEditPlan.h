#pragma once

#include "core/container/ContainerFinder.h"
#include "core/sign/SignProtection.h"
#include "core/sign/ShopSign.h"
#include "util/i18n/Text.h"

#include <string>
#include <string_view>

class BlockPos;
class BlockSource;
class ServerPlayer;
namespace ss::shop {

struct ShopConfig;


enum class SignEditOutcome {
    NotAShopSign, ///< 不像商店牌：包原样放行，绝不动玩家的牌子
    Rejected,     ///< 像商店但写错了：丢弃这个包，牌子保留原文本
    Normalized,   ///< 合法：把规范化后的四行写回包
};

struct SignEditResult {
    SignEditOutcome outcome = SignEditOutcome::NotAShopSign;
    Text            message;        ///< Rejected 时发给玩家的原因
    std::string     normalizedText; ///< Normalized 时写回 `event.text()`
    /// Normalized 时的归属交接；由事件层在 After 事件里交给 `recordShopOwner`。
    /// 结构体本身在 `core/sign/SignProtection.h`（它是纯数据 + 一个判据）。
    SignOwnerHandoff ownerHandoff;
};

/// 解析错误 → 玩家提示文案（中文，面向服主与玩家）。
Text messageFor(ShopSignError error, int maxShopAmount);

/// 附着容器的错误 → 玩家提示文案。`readShop` 与建店路径共用同一句。
Text messageFor(ContainerError error);

/// 判断一次告示牌正面编辑会把商店带到什么状态。
///
/// 只做决策：不写方块、不发消息、不取消事件——这些交给事件层。
///
/// 两道闸，都只对「正面看起来已经是商店牌」的牌子生效（普通告示牌一个字都不能碰，
/// DESIGN §5.1.1）。判据是 `looksLikeShopSign`——「像商店」而不是「现在严格合法」：
/// 改了 `maxShopAmount`、换了 `adminShopName`、或者牌子是外部工具写的脏数据，
/// 都不应该让保护失效（那也是一个便宜动作就能绕开保护）。规矩与文案都在
/// `signTextEditRejection` 一处。
SignEditResult planSignEdit(
    BlockSource&      region,
    BlockPos const&   signPos,
    ServerPlayer&     editor,
    std::string_view  rawText,
    ShopConfig const& config
);

} // namespace ss::shop
