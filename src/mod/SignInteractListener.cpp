#include "mod/SignInteractListener.h"

#include "mod/Clock.h"
#include "mod/ContainerFinderBds.h"
#include "mod/Shop.h"
#include "mod/ShopForm.h"
#include "mod/ShopService.h"
#include "shop/SignProtection.h"
#include "shop/TradeGate.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"

#include "mc/world/item/ItemStack.h"
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

    // 先判「这一格是不是商店牌」。不是就一个字都不动：普通告示牌的原版编辑界面照常弹。
    // 判据只看正面文本与记录的店主，不看箱子——箱子被拆了牌面仍然受保护（DESIGN §9.2）。
    auto const interaction = inspectShop(region, pos, player, *config);
    if (!interaction) {
        return;
    }

    // 手里那件物品会改牌子吗？商店牌上的染色/墨囊/上蜡/除蜡都归「牌面防护」管（DESIGN §9.2），
    // 要不要拦、拦下给谁，只由 `planSignTouch` 一处决定。
    // 空手时不去问物品名：`ItemStack::getTypeName()` 靠手里的 `Item` 指针。
    auto const decision = planSignTouch({
        .touch       = event.item().isNull() ? SignTouch::None : classifySignTouch(event.item().getTypeName()),
        .actor       = interaction->actor,
        .sneaking    = player.isSneaking(),
        .waxed       = interaction->waxed,
        .facingFront = interaction->facingFront,
    });

    // 放行 = 不取消这次交互：原版怎么处理就怎么处理（染色、墨囊、上蜡、除蜡，
    // 以及潜行右键进原版编辑界面）。
    if (decision.action == SignTouchAction::PassThrough) {
        return;
    }

    // 拦下并告诉玩家为什么（非店主拿染料/墨囊/蜜蜡/斧头、店主想改上了蜡的牌）。
    // 这条路不弹表单，所以不吃连点冷却：否则一次被拦的染色会把下一次点击吞掉。
    if (decision.action == SignTouchAction::Block) {
        event.cancel();
        player.sendMessage(decision.message);
        return;
    }

    // 店暂时停摆（附着格不是可用容器）：没有表单可弹。店主与 OP 仍然可以改牌面
    // （放行给原版编辑界面，否则店坏了就永远修不回来），其余人拦下——牌还有主。
    if (!interaction->shop) {
        if (canManageShop(interaction->actor)) {
            return;
        }
        event.cancel();
        player.sendMessage(shopBoxMissingMessage());
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

    openShopForm(player, sign, interaction->shop->info, interaction->role);
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
