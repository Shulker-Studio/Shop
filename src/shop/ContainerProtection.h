#pragma once

#include "shop/SignProtection.h"

#include <cstdint>
#include <string>

namespace ss::shop {

/// 对一块被商店占用的方块（商店箱或商店牌）做的一件事。
///
/// 放行条件不同，所以它们是一个枚举的取值，而不是几个 bool：开箱对店主有一道配置开关，
/// 拆箱/拆牌/拆依附方块没有。`BreakSignSupport` 的放行条件与 `BreakSign` 相同，
/// 单列出来只因为说法不同——玩家拆的是「一块普通方块」，提示必须点出后果
/// （牌会掉、商店会没），否则他不知道自己碰了什么（DESIGN §6.2 H3e）。
enum class ShopBlockTouch : std::uint8_t {
    OpenChest,        ///< 右键打开商店箱（`allowOwnerOpenChest` 只管店主）
    BreakChest,       ///< 攻击 / 拆除商店箱
    BreakSign,        ///< 攻击 / 拆除商店牌
    BreakSignSupport, ///< 拆除商店牌**依附的那一格**（拆了牌子会物理掉落，商店随之消失）
};

/// 一次判定的既成事实。身份由调用方用 `shopActorOf` 算好再传进来——
/// ADR-0005 的店主判定只写在那一个函数里。
struct ShopBlockTouchContext {
    ShopBlockTouch touch = ShopBlockTouch::OpenChest;
    ShopActor      actor = ShopActor::Other;
    /// 配置 `allowOwnerOpenChest`（DESIGN §9.2 / §15）：只影响店主，不影响 OP。
    bool allowOwnerOpenChest = true;
    /// 商店牌上记的店主显示名，只用来拼提示。
    std::string ownerName;
    /// 管理员商店：管理权只给 OP（DESIGN §11），提示里不该出现「店主」。
    bool adminShop = false;
};

struct ShopBlockDecision {
    bool allowed = true;
    /// 只有 `allowed == false` 时非空：发给玩家的一句话。
    Text message;
};

/// 判定一次对商店箱 / 商店牌的操作（DESIGN §9.2 的「商店箱保护」那一组）。
///
/// 规则：
/// - 开箱：OP 恒可；店主看 `allowOwnerOpenChest`（关掉时连店主也打不开，只能找 OP）；
/// - 拆箱 / 拆牌：店主与 OP 可，其余人不可（拆牌即关店，§4.2）；
/// - 没记下店主的牌（`NoRecordedOwner`）：只给 OP。这里与「改牌面文本」的 fail-open
///   故意不同——文本是牌子自己的事，开箱/拆箱动的是库存；一个查不到主人的箱子
///   不该因为没有主人就变成谁都能搬。
///
/// 一块方块同时被多块牌绑着（脏数据）时，调用方逐块判定：**任何一块不放行就不放行**。
///
/// `BreakSignSupport` 判的是「这一格上挂着一块商店牌」，不是「这一格是商店牌」：
/// 商店牌可以挂在石头墙上（牌面判据不看附着容器，§9.2），拆掉那面墙等于拆牌。
ShopBlockDecision planShopBlockTouch(ShopBlockTouchContext const& context);

} // namespace ss::shop
