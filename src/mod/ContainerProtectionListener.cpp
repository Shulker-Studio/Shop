#include "mod/ContainerProtectionListener.h"

#include "mod/ContainerFinderBds.h"
#include "mod/Shop.h"
#include "shop/ContainerProtection.h"

#include "ila/event/world/actor/player/PlayerAttackBlockEvent.h"
#include "ila/event/world/actor/player/PlayerOpenContainerEvent.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerDestroyBlockEvent.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/block/ActorChangeContext.h"
#include "mc/world/level/block/BlockChangeContext.h"

#include <optional>
#include <utility>
#include <vector>

namespace ss::shop {
namespace {

ll::event::ListenerPtr gOpenContainerListener;
ll::event::ListenerPtr gAttackBlockListener;
ll::event::ListenerPtr gDestroyBlockListener;

using OpenContainerBeforeEvent = ila::mc::world::actor::player::PlayerOpenContainerBeforeEvent;
using AttackBlockBeforeEvent   = ila::mc::world::actor::player::PlayerAttackBlockBeforeEvent;
using DestroyBlockEvent        = ll::event::player::PlayerDestroyBlockEvent;

/// 这一格方块对商店保护来说是什么。
struct ShopBlockSubject {
    bool                          isSign = false; ///< true = 商店牌；false = 商店箱
    ShopOwner                     signOwner;      ///< `isSign` 时有效
    std::vector<AttachedShopSign> boundSigns;     ///< 商店箱上绑着的牌；`isSign` 时为空
};

/// 把「这一格方块」识别成保护对象。空 = 与商店无关，交给原版。
///
/// 告示牌不是容器，两个分支不会同时命中；牌子优先（DESIGN §9.2：拆牌即关店）。
std::optional<ShopBlockSubject> shopBlockAt(BlockSource& region, BlockPos const& pos, ShopConfig const& config) {
    if (auto owner = shopSignOwnerAt(region, pos, config)) {
        return ShopBlockSubject{.isSign = true, .signOwner = std::move(*owner)};
    }
    auto boundSigns = attachedShopSignsAt(region, pos, config);
    if (boundSigns.empty()) {
        return std::nullopt;
    }
    return ShopBlockSubject{.isSign = false, .boundSigns = std::move(boundSigns)};
}

/// 把「商店牌上记的店主」换成一次判定。身份只由 `shopActorOf` 算（ADR-0005）。
ShopBlockDecision decide(ShopOwner const& owner, ShopBlockTouch touch, Player const& player, ShopConfig const& config) {
    auto const actor = shopActorOf(owner.xuid, player.getXuid(), player.isOperator(), owner.adminShop);
    return planShopBlockTouch({
        .touch               = touch,
        .actor               = actor,
        .allowOwnerOpenChest = config.allowOwnerOpenChest,
        .ownerName           = owner.displayName,
        .adminShop           = owner.adminShop,
    });
}

/// 一组商店牌对同一种操作放不放行：任何一块不放行就不放行（脏数据时不会放宽保护）。
ShopBlockDecision judgeSigns(
    std::vector<AttachedShopSign> const& signs,
    ShopBlockTouch                       touch,
    Player const&                        player,
    ShopConfig const&                    config
) {
    for (auto const& sign : signs) {
        auto decision = decide(sign.owner, touch, player, config);
        if (!decision.allowed) {
            return decision;
        }
    }
    return {};
}

/// 这一格商店方块对这次操作放不放行。`signTouch` / `chestTouch` 区分拆的是牌还是箱。
ShopBlockDecision judge(
    ShopBlockSubject const& subject,
    ShopBlockTouch          signTouch,
    ShopBlockTouch          chestTouch,
    Player const&           player,
    ShopConfig const&       config
) {
    return subject.isSign ? decide(subject.signOwner, signTouch, player, config)
                          : judgeSigns(subject.boundSigns, chestTouch, player, config);
}

/// 拦下并把原因告诉玩家：这两件事永远一起做，别在每条路径上各写一遍。
template <class Event>
void reject(Event& event, Player& player, ShopBlockDecision const& decision) {
    event.cancel();
    player.sendMessage(decision.message);
}

/// 右键打开容器：普通容器原版怎么处理就怎么处理。
void onOpenContainer(OpenContainerBeforeEvent& event) {
    auto const& config = Shop::getInstance().config();
    if (!config) {
        return; // 配置没加载成功时模组不会启用，这里只是兜底
    }

    auto&                   player   = event.self();
    auto&                   region   = player.getDimensionBlockSource();
    auto const              signs    = attachedShopSignsAt(region, event.containerBlockPos(), *config);
    ShopBlockDecision const decision = judgeSigns(signs, ShopBlockTouch::OpenChest, player, *config);
    if (!decision.allowed) {
        reject(event, player, decision);
    }
}

/// 左键攻击方块：在挖掘进度开始之前就拦下，免得非店主对着商店箱徒劳地挖。
void onAttackBlock(AttackBlockBeforeEvent& event) {
    auto const& config = Shop::getInstance().config();
    if (!config) {
        return;
    }

    auto&      player  = event.self();
    auto&      region  = player.getDimensionBlockSource();
    auto const subject = shopBlockAt(region, event.pos(), *config);
    if (!subject) {
        return;
    }
    ShopBlockDecision const decision =
        judge(*subject, ShopBlockTouch::BreakSign, ShopBlockTouch::BreakChest, player, *config);
    if (!decision.allowed) {
        reject(event, player, decision);
    }
}

/// 拆除方块：权威兜底（攻击被拦下之后仍可能有别的路径把方块拆掉）。
void onDestroyBlock(DestroyBlockEvent& event) {
    auto const& config = Shop::getInstance().config();
    if (!config) {
        return;
    }

    auto&      player  = event.self();
    auto&      region  = player.getDimensionBlockSource();
    auto const subject = shopBlockAt(region, event.pos(), *config);
    if (!subject) {
        return;
    }

    ShopBlockDecision const decision =
        judge(*subject, ShopBlockTouch::BreakSign, ShopBlockTouch::BreakChest, player, *config);
    if (!decision.allowed) {
        reject(event, player, decision);
        return;
    }

    // 拆牌：放行即关店——保护状态从牌子推导（ADR-0002），牌子没了箱子立刻恢复成普通箱子。
    if (subject->isSign) {
        return;
    }

    // 拆商店箱（双箱的任意一半都算）：先把绑在上面的商店牌按原版语义打掉（有掉落），
    // 再让这一格继续被拆。拆掉的是双箱里**不**挨着牌子的那一半时，另一半还在，
    // 牌子不会因失去支撑而掉落；不打掉的话商店会带着半份库存继续营业（DESIGN §13 第 34 条）。
    // 打掉的是挨着牌子的那一半时同样是这条路径：牌子先没了，原版就不会再掉一次。
    BlockChangeContext const changeSource{
        .mContextSource         = ActorChangeContext{&player},
        .mTickSchedulingEnabled = false,
    };
    for (auto const& sign : subject->boundSigns) {
        player.getLevel().destroyBlock(region, toBlockPos(sign.sign.pos), /*dropResources=*/true, changeSource);
    }
}

} // namespace

void registerContainerProtectionListener() {
    if (gOpenContainerListener || gAttackBlockListener || gDestroyBlockListener) {
        return;
    }
    auto& bus              = ll::event::EventBus::getInstance();
    gOpenContainerListener = bus.emplaceListener<OpenContainerBeforeEvent>(onOpenContainer);
    gAttackBlockListener   = bus.emplaceListener<AttackBlockBeforeEvent>(onAttackBlock);
    gDestroyBlockListener  = bus.emplaceListener<DestroyBlockEvent>(onDestroyBlock);
    if (!gOpenContainerListener || !gAttackBlockListener || !gDestroyBlockListener) {
        Shop::getInstance().getSelf().getLogger().error("注册商店箱保护监听失败");
    }
}

void unregisterContainerProtectionListener() {
    auto& bus = ll::event::EventBus::getInstance();
    if (gDestroyBlockListener) {
        bus.removeListener<DestroyBlockEvent>(gDestroyBlockListener);
        gDestroyBlockListener.reset();
    }
    if (gAttackBlockListener) {
        bus.removeListener<AttackBlockBeforeEvent>(gAttackBlockListener);
        gAttackBlockListener.reset();
    }
    if (gOpenContainerListener) {
        bus.removeListener<OpenContainerBeforeEvent>(gOpenContainerListener);
        gOpenContainerListener.reset();
    }
}

} // namespace ss::shop
