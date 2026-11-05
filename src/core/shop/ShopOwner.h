#pragma once

#include "util/config/Config.h"
#include "core/sign/ShopSign.h"
#include "core/sign/SignProtection.h"

#include <string>

class BlockPos;
class BlockSource;
class ServerPlayer;
class SignBlockActor;

namespace ss::shop {

/// 取这一格的告示牌方块实体；不是告示牌（或没有方块实体）时返回空。
///
/// 告示牌与悬挂牌都算，形态判定交给 `readSignGeometry`。
SignBlockActor* signBlockActorAt(BlockSource& region, BlockPos const& pos);

/// 一块商店牌的店主身份。来源是牌子正面的 `SignBlockActor::Text::mLastEditXuid`
/// （ADR-0005），与第 1 行的文字无关——第 1 行只是显示名。
struct ShopOwner {
    /// 正面记录的编辑者 XUID。引擎没写时是空串（DESIGN §6.5 第 3 条）。
    std::string xuid;
    /// 由 XUID 反查到的当前名字；查不到时退回牌面第 1 行的文字，再查不到就是空串。
    std::string displayName;
    /// 管理员商店（第 1 行 == `adminShopName`）。管理权只给 OP，见 DESIGN §11。
    bool adminShop = false;
};

/// 读取店主。
///
/// `parsedFront` 必须是这块牌子正面文本的解析结果（`parseShopSignText(front, true, config)`）——
/// 传进来而不是在这里重解析，是因为调用方刚算过一次，一次点击不该解析两遍。
///
/// 管理员商店按**牌面文字**判定，不按「此刻是谁在看」判定：如果用点击者的 OP 状态
/// 冒充 `editorIsOp`，非 OP 访客会把已有的管理员店解析成普通店，店主名也会错。
ShopOwner readShopOwner(SignBlockActor& sign, ShopSignResult const& parsedFront);

/// 解析这块牌子**正面**的文本。
///
/// `editorIsOp` 固定传 `true`：它只影响解析结果里的管理员店标记，不影响「是不是商店牌」。
/// 拿点击者的 OP 状态冒充它，会让非 OP 访客把已有的管理员店解析成普通店。
/// 这个反直觉的常量只写在这里，读牌面的地方一律调它，不各写一遍。
ShopSignResult parseShopSignFront(SignBlockActor& sign, ShopSignConfig const& config);

/// 这块牌的正面是不是一块合法商店牌（只看文本，不看容器）。
bool isShopSignFront(SignBlockActor& sign, ShopSignConfig const& config);

/// 按 `handoff` 落地归属（DESIGN §5.4）：牌子第一次成为商店时写给编辑者，
/// 之后改牌面不改归属（管理员店 ⇄ 普通店互转除外，那算换了个店，见 `keepsPreviousOwner`）。
///
/// 「该不该保住原店主」只由 `SignOwnerHandoff::keepsPreviousOwner` 判，
/// 第 1 行写谁也是那个判据——两边不允许各写一套条件。
///
/// 这条同 06 之后变得重要：OP 允许代改别人的牌面（DESIGN §9.2），而引擎也可能
/// 自己改掉 `mLastEditXuid`（§6.5 第 3 条）；两种情况下都不能让归属跟着编辑者走，
/// 否则 OP 改一下就把原店主锁在自己的店外了。
///
/// 三个条件同时成立才动手：正面仍是合法商店牌（`parseShopSignFront(...).ok()`）、附着容器可用、
/// 算出来的目标 XUID 与牌上记的不一致。普通牌、非法牌、失去容器的牌一个字都不碰。
///
/// 写入的是**当前文本本身**，不 refresh——客户端已经通过包拿到同一份文本，
/// 再刷一次只会抖一下。
///
/// `config` 用完整配置，因为要复用附着容器判定（`shopContainers` 白名单）。
void recordShopOwner(
    BlockSource&            region,
    BlockPos const&         signPos,
    ServerPlayer&           editor,
    ShopConfig const&       config,
    SignOwnerHandoff const& handoff
);

} // namespace ss::shop
