#include "event/ContentProtectionHooks.h"

#include "util/i18n/TextRender.h"

#include "core/container/ContainerFinderBds.h"
#include "util/log/DebugLog.h"
#include "event/HookSlots.h"
#include "Shop.h"
#include "core/container/ContentProtection.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/world/Container.h"
#include "mc/world/actor/Hopper.h"
#include "mc/world/actor/Mob.h"
#include "mc/world/actor/ai/goal/TransportItemsGoal.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/DropperBlockActor.h"

#include <optional>

// 五条 hook 放在具名命名空间而不是匿名命名空间：宏展开里有一个 `inline static` 的
// 注册计数，匿名命名空间会让它变成内部链接，clang-cl 于是报 unused-variable。
namespace ss::shop::detail {

/// 这一次搬运的目标格是不是商店箱。`config` 由调用方判过非空。
///
/// **不能用 `isShopBlockAt`**：这四条 hook 是每 tick 跑的（漏斗 20 次/秒、「红石时钟」的投掷器同理），
/// 一片农场的漏斗数会把它乘成一个真实的热点。它们只往**容器**里搬，所以只问
/// `isShopContainerBlockAt`：先一层「是不是箱子/木桶」的粗筛，漏斗链、熔炉、投掷器自身
/// 都在那里被挡掉，不必每 tick 扫六个邻居、也不必每 tick 拷两份 `ShopSignConfig`。
bool isShopContainerAt(BlockSource& region, Cell const& cell, ShopConfig const& config) {
    return isShopContainerBlockAt(region, toBlockPos(cell), config);
}

// ---------------------------------------------------------------------------
// H2a-1 漏斗抽取：源容器（漏斗上方那一格）是商店箱就抽不动
// ---------------------------------------------------------------------------

/// 方块漏斗与漏斗矿车共用这一条：`Hopper` 是两者共有的那层 actor 逻辑
/// （`Hopper::mIsEntity` 区分形态），所以一条 hook 覆盖两种形态（DESIGN §6.2 H2a）。
LL_TYPE_INSTANCE_HOOK(
    HopperPullInHook,
    HookPriority::Normal,
    Hopper,
    &Hopper::_tryPullInItemsFromAboveContainer,
    bool,
    BlockSource& region,
    Container&   toContainer,
    Vec3 const&  pos
) {
    auto const* config = currentShopConfig();
    if (config == nullptr) {
        return origin(region, toContainer, pos);
    }

    // `pos` 是漏斗（或漏斗矿车）自己的位置，源容器固定在它上方一格
    Cell const source = cellAbove(toCell(BlockPos(pos)));
    if (shouldSkipContentTransfer(isShopContainerAt(region, source, *config))) {
        if (shopDebugEnabled()) {
            shopDebugEventThrottled(
                "content_block",
                {dbgField("kind", "hopper_pull"),
                 dbgField("hopper", dbgPos(region.getDimensionId(), pos.x, pos.y, pos.z)),
                 dbgField("source", dbgPos(region.getDimensionId(), source.x, source.y, source.z))}
            );
        }
        // 跳过这一次抽取：商店箱里的东西原样留着，漏斗下一 tick 照常工作
        return false;
    }
    return origin(region, toContainer, pos);
}

// ---------------------------------------------------------------------------
// H2a-2 漏斗推出：目标容器（`attachedFace` 那一格）是商店箱就塞不进
// ---------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    HopperPushOutHook,
    HookPriority::Normal,
    Hopper,
    &Hopper::_pushOutItems,
    bool,
    BlockSource& region,
    Container&   fromContainer,
    Vec3 const&  position,
    int          attachedFace
) {
    auto const* config = currentShopConfig();
    if (config == nullptr) {
        return origin(region, fromContainer, position, attachedFace);
    }

    // `attachedFace` 是搬运的方向（基岩 `facing_direction` 编号，见 `cellInDirection`）；
    // 方向脏到判不出来就放行——猜一个方向去拦，代价是误伤别的容器。
    if (auto const target = cellInDirection(toCell(BlockPos(position)), attachedFace)) {
        if (shouldSkipContentTransfer(isShopContainerAt(region, *target, *config))) {
            if (shopDebugEnabled()) {
                shopDebugEventThrottled(
                    "content_block",
                    {dbgField("kind", "hopper_push"),
                     dbgField("hopper", dbgPos(region.getDimensionId(), position.x, position.y, position.z)),
                     dbgField("target", dbgPos(region.getDimensionId(), target->x, target->y, target->z)),
                     dbgField("face", (long long)attachedFace)}
                );
            }
            return false;
        }
    }
    return origin(region, fromContainer, position, attachedFace);
}

// ---------------------------------------------------------------------------
// H2d 投掷器：入口只开一个作用域，内层让容器查找交不出商店箱
// ---------------------------------------------------------------------------

/// 投掷器这一次搬运的守卫状态。
///
/// 字段是一起变的，所以收在一个结构里而不是几个散落的全局：
/// - `blockedTarget`：只在 `pushOutItems` 的作用域内有值，就是这一次要挡的目标格；
/// - `reached`：这个作用域里内层查找**真的被问到、而且问的正是那一格**；
/// - `lookedUp`：内层查找至少被问过一次（只为把 warn 的原因说清楚）；
/// - `warned`：那条 warn 只报一次（红石时钟会每 tick 走到这里）。
///
/// `blockedTarget` 为什么不直接在 `pushOutItems` 里提前返回：那会把物品留在投掷器里
/// （喷不出去），而票面要的是「塞不进去，物品落在地上」。原版在找不到目标容器时本来就会
/// 把物品喷到地上，所以这里只把「那一格有容器」这件事藏起来一次搬运的时间。
///
/// `reached` 为假就意味着**这一次没拦住**，两种情况都算失效、都要报出来，不许静默：
/// - `lookedUp` 为假：`getContainerAt` 的 hook 没挂上（符号漂移），或这条版本的
///   `pushOutItems` 不经过它；
/// - `lookedUp` 为真但坐标对不上：`getContainerAt` 收到的 `Vec3` 不是目标格的方块原点
///   （中心点、加上某个偏移），我们的坐标口径与它对不上。
struct DropperTransferGuard {
    std::optional<Cell> blockedTarget;
    bool                reached  = false;
    bool                lookedUp = false;
    bool                warned   = false;
};

/// 投掷器只在服务端主线程 tick，作用域不会跨线程，所以用 thread_local。
thread_local DropperTransferGuard gDropperGuard;

/// `pushOutItems` 一次的守卫：进入时记下要挡的目标格，退出时清掉。
struct DropperBlockScope {
    explicit DropperBlockScope(Cell target) {
        gDropperGuard.blockedTarget = target;
        gDropperGuard.reached       = false;
        gDropperGuard.lookedUp      = false;
    }
    ~DropperBlockScope() { gDropperGuard.blockedTarget.reset(); }
    DropperBlockScope(DropperBlockScope const&)            = delete;
    DropperBlockScope& operator=(DropperBlockScope const&) = delete;
};

LL_TYPE_INSTANCE_HOOK(
    DropperPushOutHook,
    HookPriority::Normal,
    DropperBlockActor,
    &DropperBlockActor::pushOutItems,
    bool,
    BlockSource& region
) {
    auto const* config = currentShopConfig();
    if (config == nullptr) {
        return origin(region);
    }

    Cell const device = toCell(this->getPosition());

    auto const facing = facingDirectionAt(region, this->getPosition());
    if (!facing) {
        return origin(region);
    }
    auto const target = cellInDirection(device, *facing);
    if (!target) {
        return origin(region);
    }

    if (!shouldSkipContentTransfer(isShopContainerAt(region, *target, *config))) {
        // 朝着的不是商店箱：原样放行，普通容器照常收货
        return origin(region);
    }

    DropperBlockScope const scope{*target};
    bool const              pushed = origin(region);
    if (shopDebugEnabled()) {
        shopDebugEventThrottled(
            "content_block",
            {dbgField("kind", "dropper_push"),
             dbgField("dropper", dbgPos(region.getDimensionId(), device.x, device.y, device.z)),
             dbgField("target", dbgPos(region.getDimensionId(), target->x, target->y, target->z)),
             dbgField("intercepted", gDropperGuard.reached)}
        );
    }
    if (!gDropperGuard.reached && !gDropperGuard.warned) {
        gDropperGuard.warned = true;
        // 「一次都没被问过」和「问了但坐标对不上」是两种失效，日志要能分开
        Shop::getInstance().getSelf().getLogger().warn(ui::msg(
                gDropperGuard.lookedUp ? "shop.log.protection.dropper_mismatch"
                                       : "shop.log.protection.dropper_not_asked"
            )
        );
    }
    return pushed;
}

/// 内层：只在 `pushOutItems` 为商店箱开的那一小段时间里，对这一格返回空。
///
/// `getContainerAt` 是通用的静态查找函数，别的调用方（其他容器逻辑）问同一格时
/// 不该被我们影响，所以判定包在作用域里而不是无条件生效。
LL_TYPE_STATIC_HOOK(
    DropperContainerLookupHook,
    HookPriority::Normal,
    DropperBlockActor,
    &DropperBlockActor::getContainerAt,
    Container*,
    BlockSource& region,
    Vec3 const&  pos
) {
    if (gDropperGuard.blockedTarget.has_value()) {
        gDropperGuard.lookedUp = true;
        if (toCell(BlockPos(pos)) == *gDropperGuard.blockedTarget) {
            gDropperGuard.reached = true;
            // 交不出容器 → 原版走「喷到地上」的分支（票面第 8 条要的就是这个）
            return nullptr;
        }
    }
    return origin(region, pos);
}

// ---------------------------------------------------------------------------
// H2c 铜傀儡：即将开始的取/放事件里丢掉一次
// ---------------------------------------------------------------------------

/// 只有「即将成功的取 / 即将成功的放」值得拦；「会失败」与「交互结束」原样放行——
/// 拦前者没有意义（本来就不会搬东西），拦后者会让 goal 卡在交互中。
bool isTransferStartEvent(TransportItemsGoal::Event event) {
    return event == TransportItemsGoal::Event::StartTakeInteractionWillSucceed
        || event == TransportItemsGoal::Event::StartPlaceInteractionWillSucceed;
}

LL_TYPE_INSTANCE_HOOK(
    GolemTransportStartHook,
    HookPriority::Normal,
    TransportItemsGoal,
    &TransportItemsGoal::_executeEvent,
    void,
    TransportItemsGoal::Event event
) {
    auto const* config = currentShopConfig();
    if (config != nullptr && isTransferStartEvent(event)) {
        auto const& target = this->mTargetContainerPosition.get();
        if (target.has_value()) {
            Cell const cell   = toCell(*target);
            auto&      region = this->mMob.getDimensionBlockSource();
            if (shouldSkipContentTransfer(isShopContainerAt(region, cell, *config))) {
                if (shopDebugEnabled()) {
                    shopDebugEventThrottled(
                        "content_block",
                        {dbgField("kind",
                                  event == TransportItemsGoal::Event::StartTakeInteractionWillSucceed ? "golem_take"
                                                                                                    : "golem_place"),
                         dbgField("target", dbgPos(region.getDimensionId(), cell.x, cell.y, cell.z))}
                    );
                }
                // 只丢掉这一次「即将开始的取/放」，goal 本身照常：不碰 canUse / canContinueToUse，
                // 铜傀儡不会因此变成站桩（DESIGN §6.2 H2c 的粒度讨论与 12 的验收项）。
                return;
            }
        }
    }
    origin(event);
}

// ---------------------------------------------------------------------------
// 注册 / 反注册
// ---------------------------------------------------------------------------

/// 这一条 hook 的名单。表结构与「挂上 / 逆序解开」的实现在 `mod/HookSlots.h`，
/// 与 08 的破坏防护共用一份。
HookSlot gContentSlots[] = {
    {"Hopper::_tryPullInItemsFromAboveContainer",
     [] { return HopperPullInHook::hook(); },
     [] { return HopperPullInHook::unhook(); }},
    {"Hopper::_pushOutItems",
     [] { return HopperPushOutHook::hook(); },
     [] { return HopperPushOutHook::unhook(); }},
    {"DropperBlockActor::pushOutItems",
     [] { return DropperPushOutHook::hook(); },
     [] { return DropperPushOutHook::unhook(); }},
    {"DropperBlockActor::getContainerAt",
     [] { return DropperContainerLookupHook::hook(); },
     [] { return DropperContainerLookupHook::unhook(); }},
    {"TransportItemsGoal::_executeEvent",
     [] { return GolemTransportStartHook::hook(); },
     [] { return GolemTransportStartHook::unhook(); }},
};

} // namespace ss::shop::detail

namespace ss::shop {

void registerContentProtectionHooks() {
    // 每次挂载都从干净的守卫状态开始：否则上一次卸载/重载留下的「已经报过 warn」
    // 会让这条诊断永久沉默（票面第 10 条要的就是不静默）。
    detail::gDropperGuard = {};
    attachHooks(detail::gContentSlots);
}

void unregisterContentProtectionHooks() { detachHooks(detail::gContentSlots); }

std::span<HookSlot const> contentHookSlots() { return detail::gContentSlots; }

} // namespace ss::shop
