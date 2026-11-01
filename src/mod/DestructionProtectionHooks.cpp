#include "mod/DestructionProtectionHooks.h"

#include "mod/ContainerFinderBds.h"
#include "mod/HookSlots.h"
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

#include <type_traits>
#include <vector>

// 五条 hook 放在具名命名空间而不是匿名命名空间：宏展开里有一个 `inline static` 的
// 注册计数，匿名命名空间会让它变成内部链接，clang-cl 于是报 unused-variable。
namespace ss::shop::detail {

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
    auto const* config = currentShopConfig();
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
    auto const* config = currentShopConfig();
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

/// 凋灵正在破坏吗。嵌套计数而不是 bool：将来若出现重入，外层没结束前不该提前放开守卫。
bool witherDestroyInProgress() { return gWitherDestroyDepth > 0; }

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
    if (witherDestroyInProgress()) {
        auto const* config = currentShopConfig();
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
    auto const* config = currentShopConfig();
    if (config != nullptr && isShopBlockAt(region, pos, *config)) {
        // 这一格不烧：火焰照常蔓延、旁边的木头照常烧掉（ADR-0004）
        return;
    }
    origin(region, pos, chance, random, age, firePos);
}

/// 这一条 hook 的注册/反注册与它的状态；接线方式与 09 的内容防偷完全相同，
/// 所以表结构与「挂上 / 逆序解开」的实现在 `mod/HookSlots.h`，这里只列名单。
HookSlot gDestructionSlots[] = {
    {"BlockEventCoordinator::sendEvent",
     [] { return ExplosionStartedHook::hook(); },
     [] { return ExplosionStartedHook::unhook(); }},
    {"PistonBlockActor::_attachedBlockWalker",
     [] { return PistonWalkHook::hook(); },
     [] { return PistonWalkHook::unhook(); }},
    {"WitherBoss::_destroyBlocks",
     [] { return WitherDestroyHook::hook(); },
     [] { return WitherDestroyHook::unhook(); }},
    {"Level::destroyBlock",
     [] { return WitherDestroyCellHook::hook(); },
     [] { return WitherDestroyCellHook::unhook(); }},
    {"FireBlock::checkBurn",
     [] { return FireBurnHook::hook(); },
     [] { return FireBurnHook::unhook(); }},
};

} // namespace ss::shop::detail

namespace ss::shop {

void registerDestructionProtectionHooks() { attachHooks(detail::gDestructionSlots); }

void unregisterDestructionProtectionHooks() { detachHooks(detail::gDestructionSlots); }

} // namespace ss::shop
