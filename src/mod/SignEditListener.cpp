#include "mod/SignEditListener.h"

#include "mod/Shop.h"
#include "mod/ShopOwner.h"
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
ll::event::ListenerPtr gSignEditedListener;

using SignEditBeforeEvent = ila::mc::world::actor::player::PlayerEditSignBeforeEvent;
using SignEditAfterEvent  = ila::mc::world::actor::player::PlayerEditSignAfterEvent;

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

/// 牌子已写入后的补写。DESIGN §5.4 的 fallback：包路径不保证写 `mLastEditXuid`，
/// 而 ADR-0005 的店主判定依赖它。只在「正面已是合法商店 + 附着容器可用 + 记录的
/// XUID 与本次编辑者不一致」时写一次，文本不变、不 refresh。
void onSignEdited(SignEditAfterEvent const& event) {
    // 背面是布告板：不读、不写、不校验（DESIGN §5.5）。
    if (event.textSide() != SignTextSide::Front) {
        return;
    }

    auto const& config = Shop::getInstance().config();
    if (!config) {
        return;
    }

    auto& player = event.self();
    ensureOwnerRecorded(player.getDimensionBlockSource(), event.pos(), player, *config);
}

} // namespace

void registerSignEditListener() {
    if (gSignEditListener) {
        return;
    }
    auto& bus          = ll::event::EventBus::getInstance();
    gSignEditListener  = bus.emplaceListener<SignEditBeforeEvent>(onSignEdit);
    gSignEditedListener = bus.emplaceListener<SignEditAfterEvent>(onSignEdited);
    if (!gSignEditListener || !gSignEditedListener) {
        Shop::getInstance().getSelf().getLogger().error("注册告示牌编辑监听失败");
    }
}

void unregisterSignEditListener() {
    auto& bus = ll::event::EventBus::getInstance();
    if (gSignEditListener) {
        bus.removeListener<SignEditBeforeEvent>(gSignEditListener);
        gSignEditListener.reset();
    }
    if (gSignEditedListener) {
        bus.removeListener<SignEditAfterEvent>(gSignEditedListener);
        gSignEditedListener.reset();
    }
}

} // namespace ss::shop
