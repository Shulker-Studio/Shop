#include "mod/ContainerProtectionListener.h"

#include "mod/BlockResync.h"
#include "mod/ContainerFinderBds.h"
#include "mod/DebugLog.h"
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
/// 「这一格是不是被商店占用」只问 `isShopBlockAt`（判据只有那一个定义，08 的四条 hook 也用它）；
/// 这里只负责在它说「是」之后，把后续判定要用的细节（店主 / 绑着的牌）拿出来。
/// （判据与细节因此会各解析一次牌面；这两个事件都按玩家操作频率走，换来一个口径是值当的。）
std::optional<ShopBlockSubject> shopSubjectAt(BlockSource& region, BlockPos const& pos, ShopConfig const& config) {
    if (!isShopBlockAt(region, pos, config)) {
        return std::nullopt;
    }
    if (auto owner = shopSignOwnerAt(region, pos, config)) {
        return ShopBlockSubject{.isSign = true, .signOwner = std::move(*owner)};
    }
    return ShopBlockSubject{.isSign = false, .boundSigns = attachedShopSignsAt(region, pos, config)};
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

/// 拆「商店牌依附的那一格」（DESIGN §6.2 H3e）：不是商店牌、也不是商店箱，
/// 但拆了它牌子会物理掉落、商店随之消失——非店主绕过「不能拆牌」的真实路径。
///
/// 放行条件与拆牌完全相同（店主与 OP），所以走同一张判定表，只有说法不同。
ShopBlockDecision
judgeSupportBlock(BlockSource& region, BlockPos const& pos, Player const& player, ShopConfig const& config) {
    return judgeSigns(
        shopSignsWithAttachedCell(region, pos, config),
        ShopBlockTouch::BreakSignSupport,
        player,
        config
    );
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
///
/// `kind` 只给调试日志分类用（开箱 / 拆箱 / 拆牌 / 拆依附方块）。原因本身仍然只有
/// `decision.message` 这一份文案——调试日志不另写一套说法。
///
/// `resync` 为真时把这一格的方块实体重发给动手的玩家：对着商店牌按下挖掘键时，
/// 客户端会先把”牌子碎掉、牌面文字没了“演出来，取消事件只拦权威状态、拦不住预演
/// （真机现象：拆不掉，但牌面文字空着，退出重进才回来）。
template <class Event>
void reject(
    Event&                  event,
    Player&                 player,
    ShopBlockDecision const& decision,
    BlockPos const&         blockPos,
    char const*             kind,
    bool                    resync = false
) {
    event.cancel();
    player.sendMessage(decision.message);
    if (resync) {
        resyncBlockActor(player, player.getDimensionBlockSource(), blockPos);
    }
    if (shopDebugEnabled()) {
        auto const& region = player.getDimensionBlockSource();
        shopDebugEvent(
            "protection_block",
            {dbgField("kind", std::string_view{kind}),
             dbgField("player", player.getXuid()),
             dbgField("pos", dbgPos(region.getDimensionId(), blockPos.x, blockPos.y, blockPos.z)),
             dbgField("reason", decision.message)}
        );
    }
}

/// 攻击/拆除一格方块前的公共前段：先判这一格是不是商店方块，不是的话再判「这格上挂着谁的商店牌」
/// （H3e），该拦就拦。
///
/// 返回空 = 这一格要么已经被拦下、要么与商店无关（含「只是挂着商店牌的普通方块，放行」），
/// 调用方直接收工；返回对象 = 这一格是商店方块且放行，后续处理交给调用方。
template <class Event>
std::optional<ShopBlockSubject>
shopSubjectOrRejected(Event& event, BlockSource& region, BlockPos const& pos, Player& player, ShopConfig const& config) {
    auto const              subject  = shopSubjectAt(region, pos, config);
    ShopBlockDecision const decision = subject
                                         ? judge(*subject, ShopBlockTouch::BreakSign, ShopBlockTouch::BreakChest, player, config)
                                         : judgeSupportBlock(region, pos, player, config);
    if (!decision.allowed) {
        // 调试分类：这一格是商店牌/商店箱，还是「挂着商店牌的普通方块」（H3e）。
        char const* kind = subject ? "break_shop" : "break_support";
        // 拆牌/拆箱被拦下时补一次方块实体重发（见 `reject` 的 `resync` 参数）。
        // `break_support` 那档这一格只是普通方块，没有方块实体，重发是空操作。
        reject(event, player, decision, pos, kind, /*resync=*/true);
        return std::nullopt;
    }
    return subject;
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
        reject(event, player, decision, event.containerBlockPos(), "open_chest");
    }
}

/// 左键攻击方块：在挖掘进度开始之前就拦下，免得非店主对着商店箱徒劳地挖。
/// 放行后不做事——拆除路径才是权威。
void onAttackBlock(AttackBlockBeforeEvent& event) {
    auto const& config = Shop::getInstance().config();
    if (!config) {
        return;
    }

    auto& player = event.self();
    auto& region = player.getDimensionBlockSource();
    (void)shopSubjectOrRejected(event, region, event.pos(), player, *config);
}

/// 拆除方块：权威兜底（攻击被拦下之后仍可能有别的路径把方块拆掉）。
void onDestroyBlock(DestroyBlockEvent& event) {
    auto const& config = Shop::getInstance().config();
    if (!config) {
        return;
    }

    auto&      player  = event.self();
    auto&      region  = player.getDimensionBlockSource();
    // 放行「挂着商店牌的普通方块」时什么都不用做：牌子会在下一拍被原版物理打落，
    // 与拆牌等价（DESIGN §4.2）；也是 H3e 与拆牌共用同一判据的地方。
    auto const subject = shopSubjectOrRejected(event, region, event.pos(), player, *config);

    // 与商店无关、已经被拦下、或者拆的就是商店牌（放行即关店：保护状态从牌子推导，ADR-0002，
    // 牌子没了箱子立刻恢复成普通箱子）——都不用再做什么。
    if (!subject || subject->isSign) {
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
