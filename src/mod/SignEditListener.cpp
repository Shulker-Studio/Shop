#include "mod/SignEditListener.h"

#include "mod/Shop.h"
#include "mod/ShopService.h"

#include "ila/event/world/actor/player/PlayerEditSignEvent.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignTextSide.h"

namespace ss::shop {
namespace {

ll::event::ListenerPtr gSignEditListener;

using SignEditBeforeEvent = ila::mc::world::actor::player::PlayerEditSignBeforeEvent;

void onSignEdit(SignEditBeforeEvent& event) {
    // 背面是布告板：不读、不写、不校验。
    if (event.textSide() != SignTextSide::Front) {
        return;
    }

    auto const& config = Shop::getInstance().config();
    if (!config) {
        return; // 配置没加载成功时模组不会启用，这里只是兜底
    }

    auto&      player = event.self();
    auto&      region = player.getDimensionBlockSource();
    auto const result = planSignEdit(region, event.pos(), player, event.text(), *config);

    switch (result.outcome) {
    case SignEditOutcome::NotAShopSign:
        return; // 普通告示牌原样放行
    case SignEditOutcome::Rejected:
        event.cancel();
        player.sendMessage(result.message);
        return;
    case SignEditOutcome::Normalized:
        // 改包而不是事后写回：服务端与客户端拿到的是同一份规范化文本。
        event.text() = result.normalizedText;
        return;
    }
}

} // namespace

void registerSignEditListener() {
    if (gSignEditListener) {
        return;
    }
    gSignEditListener = ll::event::EventBus::getInstance().emplaceListener<SignEditBeforeEvent>(onSignEdit);
    if (!gSignEditListener) {
        Shop::getInstance().getSelf().getLogger().error("注册告示牌编辑监听失败");
    }
}

void unregisterSignEditListener() {
    if (!gSignEditListener) {
        return;
    }
    ll::event::EventBus::getInstance().removeListener<SignEditBeforeEvent>(gSignEditListener);
    gSignEditListener.reset();
}

} // namespace ss::shop
