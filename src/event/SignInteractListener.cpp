#include "event/SignInteractListener.h"
#include "util/i18n/TextRender.h"

#include "event/BlockResync.h"
#include "util/time/Time.h"
#include "core/container/ContainerFinder.h"
#include "util/log/Debug.h"
#include "Shop.h"
#include "form/ShopForm.h"
#include "core/shop/ShopInspector.h"
#include "core/sign/SignProtection.h"
#include "core/trade/ClickCooldown.h"

#include "ll/api/event/EventBus.h"
#include "ll/api/event/ListenerBase.h"
#include "ll/api/event/player/PlayerInteractBlockEvent.h"

#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/actor/BlockActor.h"  // isTypeOrDerived(BlockActorType::Sign)
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"

#include <optional>
#include <vector>

namespace ss::shop {
namespace {

ll::event::ListenerPtr   gSignInteractListener;
std::optional<ClickCooldown> gTradeGate;

/// 牌面改动被拦下时，`protection_block` 的分类。
char const* signTouchKind(SignTouch touch) {
    switch (touch) {
    case SignTouch::Dye:
        return "sign_color";
    case SignTouch::InkSac:
    case SignTouch::GlowInkSac:
        return "sign_style";
    case SignTouch::HoneyComb:
        return "sign_wax";
    case SignTouch::None:
        break;
    }
    return "sign_edit";
}

/// `sign_decision` 一行所需的手势上下文。
std::vector<std::string> touchFields(
    Player const&      player,
    BlockSource const& region,
    BlockPos const&    pos,
    ItemStack const&   item,
    bool               sneaking,
    bool               waxed,
    char const*        plan
) {
    // 手里没东西时不去问物品名：`getTypeName()` 靠 `Item` 指针，空手会空指针。
    return {
        // `std::string_view` 必须显式写：`char const*` 唯一的隐式转换目标是 `bool`，
        // 直接传会静默打成 `plan=true`（真机第一轮就是这么暴露出来的）。
        dbgField("plan", std::string_view{plan}),
        dbgField("player", player.getXuid()),
        dbgField("pos", dbgPos(region.getDimensionId(), pos.x, pos.y, pos.z)),
        dbgField("item", item.isNull() ? "empty" : item.getTypeName()),
        dbgField("sneaking", sneaking),
        dbgField("waxed", waxed),
    };
}

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
        // 只对「告示牌类方块」记一行：普通方块被点一下不该进调试日志，
        // 但一块写着商店格式却没通过校验的牌，正是 D1/D2 要看的。
        if (shopDebugEnabled()) {
            if (auto* actor = region.getBlockEntity(pos);
                actor != nullptr && actor->isTypeOrDerived(BlockActorType::Sign)) {
                shopDebugEvent(
                    "sign_decision",
                    touchFields(
                        player,
                        region,
                        pos,
                        event.item(),
                        player.isSneaking(),
                        /*waxed=*/false,
                        "NotAShopSign"
                    )
                );
            }
        }
        return;
    }

    // 手里那件物品会改牌子吗？商店牌上的染色/墨囊/上蜡都归「牌面防护」管（DESIGN §9.2），
    // 要不要拦、拦下给谁，只由 `planSignTouch` 一处决定。
    // 空手时不去问物品名：`ItemStack::getTypeName()` 靠手里的 `Item` 指针。
    SignTouch const touch = event.item().isNull() ? SignTouch::None : classifySignTouch(event.item().getTypeName());
    auto const      decision = planSignTouch({
        .touch       = touch,
        .actor       = interaction->actor,
        .sneaking    = player.isSneaking(),
        .waxed       = interaction->waxed,
        .facingFront = interaction->facingFront,
    });

    // 放行 = 不取消这次交互：原版怎么处理就怎么处理（染色、墨囊、上蜡，
    // 以及潜行右键进原版编辑界面）。
    if (decision.action == SignTouchAction::PassThrough) {
        if (shopDebugEnabled()) {
            shopDebugEvent(
                "sign_decision",
                touchFields(player, region, pos, event.item(), player.isSneaking(), interaction->waxed, "PassThrough")
            );
        }
        return;
    }

    // 拦下并告诉玩家为什么（非店主拿染料/墨囊/蜜蜡/斧头、店主想改上了蜡的牌）。
    // 这条路不弹表单，所以不吃连点冷却：否则一次被拦的染色会把下一次点击吞掉。
    if (decision.action == SignTouchAction::Block) {
        if (shopDebugEnabled()) {
            shopDebugEvent(
                "sign_decision",
                touchFields(player, region, pos, event.item(), player.isSneaking(), interaction->waxed, "Block")
            );
            shopDebugEvent(
                "protection_block",
                {dbgField("kind", signTouchKind(touch)),
                 dbgField("player", player.getXuid()),
                 dbgField("pos", dbgPos(region.getDimensionId(), pos.x, pos.y, pos.z)),
                 dbgField("reason", text::render(decision.message))}
            );
        }
        event.cancel();
        ui::send(player, ui::Tone::Warning, decision.message);
        // 取消拦住了服务端的改动，但客户端已经把它预演出来了：补一份真实数据过去。
        resyncBlockActor(player, region, pos);
        return;
    }

    // 店暂时停摆（附着格不是可用容器，或牌面已不严格合法）：没有表单可弹。
    // 谁能进原版编辑界面问的是与提交阶段同一个判据（`mayEditShopSignText`）：
    // 店主与 OP 放行（店坏了要能修回来），游客拦下。
    if (!interaction->shop) {
        bool const mayEdit = mayEditShopSignText(interaction->actor);
        if (shopDebugEnabled()) {
            shopDebugEvent(
                "sign_decision",
                touchFields(
                    player,
                    region,
                    pos,
                    event.item(),
                    player.isSneaking(),
                    interaction->waxed,
                    mayEdit ? "PassThrough" : "Block"
                )
            );
        }
        if (mayEdit) {
            return;
        }
        event.cancel();
        ui::send(
            player,
            ui::Tone::Warning,
            interaction->unavailable == ShopUnavailable::NoContainer ? shopBoxMissingMessage() : shopInvalidMessage()
        );
        return;
    }

    // 冷却期内连点：拦下交互，也不再弹第二份表单（DESIGN §10.4）。
    // 注意仍然要 cancel——否则连点会退化成"弹一堆原版编辑界面"。
    Cell const sign    = toCell(pos);
    bool const allowed = gTradeGate->allow(player.getXuid(), region.getDimensionId(), sign, monotonicNowMs());
    event.cancel();
    if (shopDebugEnabled()) {
        auto fields = touchFields(
            player,
            region,
            pos,
            event.item(),
            player.isSneaking(),
            interaction->waxed,
            allowed ? "TakeOver" : "Block"
        );
        fields.emplace_back(dbgField("cooldown", !allowed));
        shopDebugEvent("sign_decision", fields);
    }
    if (!allowed) {
        return;
    }

    openShopForm(player, sign, interaction->shop->info, interaction->role, canManageShop(interaction->actor));
}

} // namespace

void registerSignInteractListener() {
    if (gSignInteractListener) {
        return;
    }
    auto const& config = Shop::getInstance().config();
    if (!config) {
        // 没有配置就没有冷却门：挂上去只会让每一下点击都落到 `onInteractBlock` 的早退分支，
        // 而那种「监听器在、但永远不干活」的状态最难查（`/shop status` 也会显示已注册）。
        Shop::getInstance().getSelf().getLogger().error(ui::msg("shop.log.protection.no_config_no_interact"));
        return;
    }
    gTradeGate.emplace(config->shopInteractionCooldownMs);
    gSignInteractListener =
        ll::event::EventBus::getInstance().emplaceListener<ll::event::player::PlayerInteractBlockEvent>(
            onInteractBlock
        );
    if (!gSignInteractListener) {
        Shop::getInstance().getSelf().getLogger().error(ui::msg("shop.log.protection.interact_register_failed"));
    }
}

void unregisterSignInteractListener() {
    if (gSignInteractListener) {
        ll::event::EventBus::getInstance().removeListener<ll::event::player::PlayerInteractBlockEvent>(
            gSignInteractListener
        );
        gSignInteractListener.reset();
    }
    // 冷却门无条件清：注册可能在 listener 挂上之前就失败，那时 gate 已经建好了。
    // 早退会把一个陈述的冷却门留给下一次 enable。
    gTradeGate.reset();
}

} // namespace ss::shop
