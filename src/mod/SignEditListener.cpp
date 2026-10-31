#include "mod/SignEditListener.h"

#include "mod/ContainerFinderBds.h"
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

/// 一次编辑的归属交接：Before 阶段判出，紧接着的 After 阶段用（DESIGN §5.4）。
///
/// 只存一条：同一块牌的两个事件是连着来的（服务端线程上一个接一个），
/// 而「上一次被 Rejected（没有 After）」的残留会在下一次 Before 时被覆写。
/// 取用前比对牌位（含维度）与编辑者：任何一边对不上就不消费，也不吃别的牌子的 After。
struct PendingOwnerHandoff {
    Cell             sign;
    int              dimension = 0; ///< `DimensionType` 的整数值：`Cell` 只描述维度内的一格
    std::string      editorXuid;
    SignOwnerHandoff handoff;
};

std::optional<PendingOwnerHandoff> gPendingOwnerHandoff;

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
        // 归属留给 After 阶段（那时文本才真写进去），先把它存下。
        gPendingOwnerHandoff = PendingOwnerHandoff{
            .sign       = toCell(event.pos()),
            .dimension  = static_cast<int>(region.getDimensionId()),
            .editorXuid = player.getXuid(),
            .handoff    = result.ownerHandoff,
        };
        return;
    }
}

/// 牌子已写入后的归属落地。DESIGN §5.4 的 fallback：包路径不保证写 `mLastEditXuid`，
/// 而 ADR-0005 的店主判定依赖它。目标归属来自 Before 阶段存下的交接——不是
/// 「记的 XUID ≠ 编辑者就覆盖」：改牌面不改归属（§5.4）。
void onSignEdited(SignEditAfterEvent const& event) {
    auto& player = event.self();

    // 先比对，匹配才消费：别的牌子、别的玩家、别的维度的事件不该吃掉这条待用的交接
    // （吃掉会让引擎没写 XUID 的牌子退化成无主牌，那是一条静默降级的路）。
    bool const matches =
        gPendingOwnerHandoff && gPendingOwnerHandoff->sign == toCell(event.pos())
        && gPendingOwnerHandoff->dimension == static_cast<int>(player.getDimensionBlockSource().getDimensionId())
        && gPendingOwnerHandoff->editorXuid == player.getXuid();

    // 背面是布告板：不读、不写、不校验（DESIGN §5.5）。
    if (event.textSide() != SignTextSide::Front) {
        return;
    }

    auto const& config = Shop::getInstance().config();
    if (!config) {
        return;
    }

    if (!matches) {
        return; // 不是我们放行的那次编辑：归属不是我们该动的事
    }

    auto handoff = std::move(gPendingOwnerHandoff->handoff);
    gPendingOwnerHandoff.reset();
    recordShopOwner(player.getDimensionBlockSource(), event.pos(), player, *config, handoff);
}

} // namespace

void registerSignEditListener() {
    if (gSignEditListener) {
        return;
    }
    auto& bus           = ll::event::EventBus::getInstance();
    gSignEditListener   = bus.emplaceListener<SignEditBeforeEvent>(onSignEdit);
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
    gPendingOwnerHandoff.reset();
}

} // namespace ss::shop
