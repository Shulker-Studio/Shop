#pragma once

#include "core/shop/ShopFormRole.h"
#include "core/shop/ShopReader.h"
#include "core/sign/SignProtection.h"

#include <cstdint>
#include <optional>

class BlockPos;
class BlockSource;
class Player;
namespace ss::shop {

struct ShopConfig;


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
    /// 表单角色：只决定买卖这一侧的视角（店主不跟自己交易、管理员店人人都是买家）。
    /// **管理入口不看它**，看 `canManageShop(actor)`（DESIGN §10.1）。
    ShopFormRole role = ShopFormRole::Visitor;
    /// 这块牌已经上蜡：原版去不掉蜡，连店主也改不了文本（DESIGN §9.2）。
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
