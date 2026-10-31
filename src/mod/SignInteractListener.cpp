#include "mod/SignInteractListener.h"

#include "mod/Clock.h"
#include "mod/ContainerFinderBds.h"
#include "mod/Shop.h"
#include "mod/ShopForm.h"
#include "mod/ShopService.h"
#include "shop/TradeGate.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include <optional>

namespace ss::shop {
namespace {

ll::event::ListenerPtr   gSignInteractListener;
std::optional<TradeGate> gTradeGate;

void onInteractBlock(ll::event::player::PlayerInteractBlockEvent& event) {
    auto const& config = Shop::getInstance().config();
    if (!config || !gTradeGate) {
        return;
    }

    auto&           player = event.self();
    BlockPos const& pos    = event.blockPos();
    auto&           region = player.getDimensionBlockSource();

    // 先判「这是不是一块可交互的商店牌」。不是就一个字都不动：
    // 普通告示牌、非法商店牌、玩家看着背面时，原版编辑界面照常弹。
    auto const interaction = inspectShop(region, pos, player, *config);
    if (!interaction) {
        return;
    }

    // 冷却期内连点：拦下交互，也不再弹第二份表单（DESIGN §10.4）。
    // 注意仍然要 cancel——否则连点会退化成"弹一堆原版编辑界面"。
    Cell const sign    = toCell(pos);
    bool const allowed = gTradeGate->allow(player.getXuid(), sign, monotonicNowMs());
    event.cancel();
    if (!allowed) {
        return;
    }

    openShopForm(player, sign, interaction->shop.info, interaction->role);
}

} // namespace

void registerSignInteractListener() {
    if (gSignInteractListener) {
        return;
    }
    auto const& config = Shop::getInstance().config();
    if (config) {
        gTradeGate.emplace(config->shopInteractionCooldownMs);
    }
    gSignInteractListener =
        ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerInteractBlockEvent>(
            onInteractBlock
        );
    if (!gSignInteractListener) {
        Shop::getInstance().getSelf().getLogger().error("注册方块交互监听失败");
    }
}

void unregisterSignInteractListener() {
    if (!gSignInteractListener) {
        return;
    }
    ll::event::EventBus::getInstance().removeListener<ll::event::player::PlayerInteractBlockEvent>(
        gSignInteractListener
    );
    gSignInteractListener.reset();
    gTradeGate.reset();
}

} // namespace ss::shop
