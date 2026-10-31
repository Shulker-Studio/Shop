#pragma once

#include "mod/Config.h"
#include "shop/ShopInfo.h"
#include "shop/ShopSign.h"
#include "shop/SignProtection.h"

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
    /// Normalized 时的归属交接；由事件层在 After 事件里交给 `recordShopOwner`。
    /// 结构体本身在 `shop/SignProtection.h`（它是纯数据 + 一个判据，可以单测）。
    SignOwnerHandoff ownerHandoff;
};

/// 解析错误 → 玩家提示文案（中文，面向服主与玩家）。
std::string messageFor(ShopSignError error, int maxShopAmount);

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

/// 牌子是商店牌、但现在不能交易的原因。交互阶段要据此告诉玩家为什么没有表单。
enum class ShopUnavailable : std::uint8_t {
    None,        ///< 能交易
    NoContainer, ///< 附着格不是可用容器（箱子被拆了、换掉了）
    NotValid,    ///< 牌面已经不严格合法（调小了 `maxShopAmount`、写了脏数据、改了管理员名）
};

/// 一次交互该给谁看什么：商店快照（能交易时）+ 身份。
struct ShopInteraction {
    /// 现在能交易吗。空 = 正面是商店牌但现在停摆了，原因在 `unavailable`。
    /// 牌面保护不看这个字段：箱子被拆了也不该让牌面变成人人可改（DESIGN §9.2）。
    std::optional<ShopSnapshot> shop;
    /// `shop` 为空的原因（`shop` 有值时恒为 `None`）。
    ShopUnavailable unavailable = ShopUnavailable::None;
    /// 这个人是谁（ADR-0005 的判定，只算一次）。
    ShopActor actor = ShopActor::Other;
    /// 表单角色，由 `canManageShop(actor)` 派生：店主与 OP 拿管理入口，其余拿买卖按钮。
    ShopFormRole role = ShopFormRole::Visitor;
    /// 这块牌已经上蜡：连店主也要先除蜡才能改文本（DESIGN §9.2）。
    /// 上蜡只是编辑开关，不影响商店语义，交易照常。
    /// 这里是「整块牌子」的属性，与 `shop` 有没有值（能不能交易）无关。
    bool waxed = false;
    /// 玩家看的是正面还是背面。背面不弹表单（决定在 `planSignTouch` 里），
    /// 但物品类操作还是受保护：上蜡改的是整块牌子（DESIGN §5.5 / §9.2）。
    bool facingFront = true;
};

/// 看待一块商店牌（点牌路径）。
///
/// 返回空 = 这一格不是商店牌：不是告示牌，或者正面文本根本不像商店牌
/// （`looksLikeShopSign`，DESIGN §5.1.1）。
///
/// 「是不是商店牌」**只看正面文本与记录的店主**，不看附着容器：容器只决定
/// 「现在能不能交易」，它不影响归属，也不影响牌面防护。
///
/// 只读取：不写方块、不发消息。
std::optional<ShopInteraction>
inspectShop(BlockSource& region, BlockPos const& signPos, Player& viewer, ShopConfig const& config);

} // namespace ss::shop
