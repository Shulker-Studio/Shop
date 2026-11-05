#include "core/shop/ShopInspector.h"

#include "core/economy/Provider.h"
#include "core/economy/Service.h"
#include "core/shop/SignOwner.h"
#include "core/sign/ShopSign.h"
#include "util/i18n/Reason.h"
#include "util/i18n/Text.h"

#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

namespace ss::shop {

std::optional<ShopInteraction>
inspectShop(BlockSource& region, BlockPos const& signPos, Player& viewer, ShopConfig const& config) {
    auto* sign = signBlockActorAt(region, signPos);
    if (sign == nullptr) {
        return std::nullopt;
    }

    auto const signConfig = toSignConfig(config);
    auto const parsed     = parseShopSignFront(*sign, signConfig);
    if (!parsed.looksLikeShopSign()) {
        return std::nullopt; // 普通告示牌：原版编辑界面照常弹
    }

    // 「谁是店主」只由 `shopActorOf` 判（ADR-0005），表单角色只是它的一种用法。
    SignOwner const owner = readSignOwner(*sign, parsed);
    ShopActor const actor = shopActorOf(owner.xuid, viewer.getXuid(), viewer.isOperator(), owner.adminShop);

    ShopInteraction interaction{
        .actor       = actor,
        .role        = shopFormRoleOf(actor, owner.adminShop),
        .waxed       = sign->mIsWaxed,
        .facingFront = sign->getSideFacingPlayer(viewer) == SignTextSide::Front,
    };

    // 牌子像商店但现在能不能交易，看附着格（DESIGN §9.3）。不能交易也照样返回：
    // 牌面保护不看容器（§9.2），只是没表单可弹。
    auto shop = readShop(region, signPos, config);
    if (!shop) {
        // 两种停摆原因分开说：箱子没了 / 牌面已经不严格合法（改了配置、写了脏数据）。
        interaction.unavailable = parsed.ok() ? ShopUnavailable::NoContainer : ShopUnavailable::NotValid;
        return interaction;
    }

    // 背面是布告板：那一面不弹表单，也就不必问余额与「能不能交易」（DESIGN §5.5）。
    if (interaction.facingFront) {
        // 余额与「能不能交易」都要问后端，只问一次。
        // `unavailableReason` 可能已经被 `readShop` 填过（箱子空），那个原因更具体，不覆盖它。
        auto& provider = economyBackend();
        if (!provider.available()) {
            // 没有可用后端：余额不编造数字（DESIGN §10.1），买卖按钮也不给。
            if (text::empty(shop->info.unavailableReason)) {
                shop->info.unavailableReason = noEconomyReason();
            }
        } else {
            // 读不到就留空：正文写「暂时读不到」，不猜一个数字（DESIGN §10.1）。
            shop->info.balance = provider.get(viewer.getXuid());
            // 管理员店的资金端是系统账户（§7.4）：后端声明不了这项能力时买卖必然失败，
            // 那就一个按钮都不给（与下面的离线策略同一套“提前藏按钮”的做法）。
            if (shop->info.adminShop) {
                if (!provider.supportsSystemAccount() && text::empty(shop->info.unavailableReason)) {
                    shop->info.unavailableReason = noSystemAccountReason();
                }
            } else if (text::empty(shop->info.unavailableReason)
                       && economy::ownerUnreachable(
                           onlinePlayer(shop->ownerXuid) != nullptr,
                           provider.supportsOffline(),
                           config.economy.requireOwnerOnline
                       )) {
                shop->info.unavailableReason = ownerOfflineReason();
            }
        }
    }

    interaction.shop = std::move(shop);
    return interaction;
}

} // namespace ss::shop
