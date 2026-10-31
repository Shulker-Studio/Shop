#include "mod/SignInteractListener.h"

#include "mod/Shop.h"
#include "mod/ContainerFinderBds.h"
#include "mod/ShopForm.h"
#include "mod/ShopService.h"
#include "shop/TradeGate.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include <chrono>
#include <optional>

namespace ss::shop {
namespace {

ll::event::ListenerPtr   gSignInteractListener;
std::optional<TradeGate> gTradeGate;

/// 单调时钟毫秒。用 `steady_clock` 而不是系统时间：改系统时间不该影响反刷。
std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

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
    auto const snapshot = inspectShop(region, pos, player, *config);
    if (!snapshot) {
        return;
    }

    // 冷却期内连点：拦下交互，也不再弹第二份表单（DESIGN §10.4）。
    // 注意仍然要 cancel——否则连点会退化成"弹一堆原版编辑界面"。
    bool const allowed = gTradeGate->allow(player.getXuid(), toCell(pos), nowMs());
    event.cancel();
    if (!allowed) {
        return;
    }

    openShopForm(player, snapshot->info, snapshot->role);
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
