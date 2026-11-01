#include "mod/DestructionProtectionHooks.h"

#include "mod/ContainerFinderBds.h"
#include "mod/Shop.h"
#include "shop/DestructionProtection.h"

#include "ll/api/memory/Hook.h"

#include "mc/deps/core/math/IRandom.h"
#include "mc/gameplayhandlers/CoordinatorResult.h"
#include "mc/world/actor/boss/WitherBoss.h"
#include "mc/world/events/BlockEventCoordinator.h"
#include "mc/world/events/EventRef.h"
#include "mc/world/events/ExplosionStartedEvent.h"
#include "mc/world/events/MutableBlockGameplayEvent.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/BlockChangeContext.h"
#include "mc/world/level/block/FireBlock.h"
#include "mc/world/level/block/actor/PistonBlockActor.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/phys/AABB.h"

#include <cstddef>
#include <type_traits>
#include <vector>

namespace ss::shop::detail {

/// 配置没加载成功时一律放行：那时模组不该在跑（`Shop::enable` 只在配置可用时调用我们），
/// 这里只是兜底——宁可放过一次爆炸，也不要对着一份空配置误判。
ShopConfig const* currentConfig() {
    auto const& config = Shop::getInstance().config();
    return config ? &*config : nullptr;
}

// ---------------------------------------------------------------------------
// H3a 爆炸：把商店方块从「这次要炸掉的格子」里删掉
// ---------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    ExplosionStartedHook,
    HookPriority::Normal,
    BlockEventCoordinator,
    &BlockEventCoordinator::sendEvent,
    CoordinatorResult,
    EventRef<MutableBlockGameplayEvent<CoordinatorResult>> event
) {
    auto const* config = currentConfig();
    if (config == nullptr) {
        return origin(event);
    }

    // 这个 coordinator 也发别的方块事件（比如 BlockTryDestroyByPlayerEvent），
    // 只有爆炸那一支是可变方块列表，别的原样放行。
    event.get().visit([&](auto&& arg) {
        using Event = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<Event, Details::ValueOrRef<ExplosionStartedEvent>>) {
            auto& exploded = arg.value();
            // 爆炸事件里没有 BlockSource，只能自己从维度拿一份（与爆炸本体同一份）
            auto& region = exploded.mDimension.getBlockSourceFromMainChunkSource();

            std::vector<DestructionTarget> targets;
            targets.reserve(exploded.mBlocks.get().size());
            for (BlockPos const& pos : exploded.mBlocks.get()) {
                targets.push_back(DestructionTarget{toCell(pos), isShopBlockAt(region, pos, *config)});
            }

            // 「放过这一格、其余照炸」的语义只写在纯逻辑里（ADR-0004）
            for (Cell const& cell : cellsToSpare(targets)) {
                exploded.mBlocks.get().erase(toBlockPos(cell));
            }
        }
    });
    return origin(event);
}

// ---------------------------------------------------------------------------
// H3b 活塞：推动路径上遇到商店方块就推不动
// ---------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    PistonWalkHook,
    HookPriority::Normal,
    PistonBlockActor,
    &PistonBlockActor::_attachedBlockWalker,
    bool,
    BlockSource&    region,
    BlockPos const& curPos,
    uchar           curBranchFacing,
    uchar           pistonMoveFacing
) {
    auto const* config = currentConfig();
    if (config != nullptr && isShopBlockAt(region, curPos, *config)) {
        // 原版把 false 读成「这一路推不动」：整次推动失败，别处的活塞照常工作。
        // 活塞没有「部分推动」的语义，所以这里不可能只挡住商店箱而让同一次推动的
        // 其他方块动起来——ADR-0004 要求的是「不取消活塞本身」，不是「拆开一次推动」。
        return false;
    }
    return origin(region, curPos, curBranchFacing, pistonMoveFacing);
}

// ---------------------------------------------------------------------------
// H3c 凋灵：破坏期间逐格剔除（不是提前返回）
// ---------------------------------------------------------------------------

/// 凋灵正在破坏的嵌套层数（线程局部：凋灵破坏发生在服务端主线程，
/// 而爆炸、玩家、我们自己去打商店牌都用不到这条守卫）。
///
/// 为什么不用「提前返回」：`WitherBoss::_destroyBlocks` 一次拿到的是整个 `AABB`，
/// 提前返回就等于把这次破坏整体取消，旁边的方块也一块不掉——正是 ADR-0004 拒绝的行为。
/// 所以这里放行原版，由 `Level::destroyBlock` 的守卫在标志期内逐格判定。
thread_local int gWitherDestroyDepth = 0;

struct WitherDestroyScope {
    WitherDestroyScope() { ++gWitherDestroyDepth; }
    ~WitherDestroyScope() { --gWitherDestroyDepth; }
    WitherDestroyScope(WitherDestroyScope const&)            = delete;
    WitherDestroyScope& operator=(WitherDestroyScope const&) = delete;
};

LL_TYPE_INSTANCE_HOOK(
    WitherDestroyHook,
    HookPriority::Normal,
    WitherBoss,
    &WitherBoss::_destroyBlocks,
    void,
    Level&                       level,
    AABB const&                  bb,
    BlockSource&                 region,
    int                          range,
    WitherBoss::WitherAttackType attackType
) {
    WitherDestroyScope const scope;
    origin(level, bb, region, range, attackType);
}

/// 凋灵破坏期间的逐格守卫。**只在这段时间里生效**——其余任何来源的拆除都不受它影响：
/// 玩家拆自己的商店（07）、我们在拆除事件里主动打掉商店牌，都走原版。
LL_TYPE_INSTANCE_HOOK(
    WitherDestroyCellHook,
    HookPriority::Normal,
    Level,
    &Level::$destroyBlock,
    bool,
    BlockSource&              region,
    BlockPos const&           pos,
    bool                      dropResources,
    BlockChangeContext const& changeSourceContext
) {
    if (gWitherDestroyDepth > 0) {
        auto const* config = currentConfig();
        if (config != nullptr && isShopBlockAt(region, pos, *config)) {
            // 这一格不受凋灵影响；同一次破坏里的其他格子继续由原版毁掉
            return false;
        }
    }
    return origin(region, pos, dropResources, changeSourceContext);
}

// ---------------------------------------------------------------------------
// H3d 火焰：烧到商店方块就跳过这一格
// ---------------------------------------------------------------------------

LL_TYPE_INSTANCE_HOOK(
    FireBurnHook,
    HookPriority::Normal,
    FireBlock,
    &FireBlock::checkBurn,
    void,
    BlockSource&    region,
    BlockPos const& pos,
    int             chance,
    IRandom&        random,
    int             age,
    BlockPos const& firePos
) {
    auto const* config = currentConfig();
    if (config != nullptr && isShopBlockAt(region, pos, *config)) {
        // 这一格不烧：火焰照常蔓延、旁边的木头照常烧掉（ADR-0004）
        return;
    }
    origin(region, pos, chance, random, age, firePos);
}

/// 每一条 hook 是否真的挂上了。手动注册的 hook 没挂上就 `unhook()` 是不对的，
/// 而 `enable`/`disable` 也不一定成对到达；逐条记账让注册与反注册都变成幂等的，
/// 也不会在某个符号漂移（注册失败）之后去反注册一个从未挂上的 hook。
struct HookedHooks {
    bool explosion  = false;
    bool piston     = false;
    bool wither     = false;
    bool witherCell = false;
    bool fire       = false;
};

HookedHooks gHooked;

} // namespace ss::shop::detail

namespace ss::shop {

void registerDestructionProtectionHooks() {
    // 返回值非 0 表示符号没挂上（多半是版本漂移）。五条各自独立：
    // 一条失败不该拖垮另外四条，所以逐条记账、不 early return。
    auto const tryHook = [](int result, char const* what) {
        if (result != 0) {
            Shop::getInstance().getSelf().getLogger().error("破坏防护 hook 注册失败：{}", what);
            return false;
        }
        return true;
    };

    // 重复注册 = 同一条路径被拦两次；已经挂上的那几条不重复挂。
    if (!detail::gHooked.explosion) {
        detail::gHooked.explosion = tryHook(detail::ExplosionStartedHook::hook(), "BlockEventCoordinator::sendEvent");
    }
    if (!detail::gHooked.piston) {
        detail::gHooked.piston = tryHook(detail::PistonWalkHook::hook(), "PistonBlockActor::_attachedBlockWalker");
    }
    if (!detail::gHooked.wither) {
        detail::gHooked.wither = tryHook(detail::WitherDestroyHook::hook(), "WitherBoss::_destroyBlocks");
    }
    if (!detail::gHooked.witherCell) {
        detail::gHooked.witherCell = tryHook(detail::WitherDestroyCellHook::hook(), "Level::destroyBlock");
    }
    if (!detail::gHooked.fire) {
        detail::gHooked.fire = tryHook(detail::FireBurnHook::hook(), "FireBlock::checkBurn");
    }
}

void unregisterDestructionProtectionHooks() {
    // 只反注册真的挂上了的那几条（注册失败的从来不在）
    if (detail::gHooked.fire) {
        detail::FireBurnHook::unhook();
        detail::gHooked.fire = false;
    }
    if (detail::gHooked.witherCell) {
        detail::WitherDestroyCellHook::unhook();
        detail::gHooked.witherCell = false;
    }
    if (detail::gHooked.wither) {
        detail::WitherDestroyHook::unhook();
        detail::gHooked.wither = false;
    }
    if (detail::gHooked.piston) {
        detail::PistonWalkHook::unhook();
        detail::gHooked.piston = false;
    }
    if (detail::gHooked.explosion) {
        detail::ExplosionStartedHook::unhook();
        detail::gHooked.explosion = false;
    }
}

} // namespace ss::shop
