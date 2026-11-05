#include "core/sign/SignEditPlan.h"

#include "util/i18n/Reason.h"
#include "core/container/ContainerFinder.h"
#include "core/shop/ItemBanList.h"
#include "core/shop/SignOwner.h"
#include "core/sign/SignProtection.h"
#include "core/sign/ShopSign.h"
#include "Shop.h"

#include "mc/server/ServerPlayer.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include <string>

namespace ss::shop {

Text messageFor(ShopSignError error, int maxShopAmount) {
    switch (error) {
    case ShopSignError::EmptyOwnerName:
        return text::keyed("shop.trade.sign.owner_empty");
    case ShopSignError::BadAmount:
        // 规则与措辞都归纯逻辑层（`amountOutOfRangeMessage`）：管理表单改件数说的是同一句。
        return amountOutOfRangeMessage(maxShopAmount);
    case ShopSignError::BadPrice:
        return text::keyed("shop.trade.sign.price_malformed");
    case ShopSignError::SellPriceAboveBuy:
        // 规则与措辞都归纯逻辑层（`sellPriceAboveBuyMessage`），这里只说规则、不念号码。
        return sellPriceAboveBuyMessage(std::nullopt, std::nullopt);
    case ShopSignError::None:
    case ShopSignError::NotAShopSign:
        break;
    }
    return text::keyed("shop.trade.sign.invalid");
}

Text messageFor(ContainerError error) {
    switch (error) {
    case ContainerError::NotAContainer:
    case ContainerError::UnknownSignShape:
        // 立在箱子旁边的地上、贴在箱子旁边的墙上、侧挂的悬挂牌都走这里。
        return text::keyed("shop.trade.sign.not_on_chest");
    case ContainerError::NotAllowedContainer:
        return text::keyed("shop.trade.sign.container_not_allowed");
    case ContainerError::Empty:
        return text::keyed("shop.trade.sign.chest_empty");
    case ContainerError::MixedContainerContents:
        // 盒子有货，只是里面装得不整齐：这不是“缺货”，不能让店主去补货。
        return text::keyed("shop.trade.sign.mixed_container_contents");
    case ContainerError::BannedItem:
        // 服主禁售：既不谈缺货也不谈整理，换一件商品才能开店。
        return text::keyed("shop.trade.sign.banned_item");
    case ContainerError::BundleProduct:
        // 收纳袋的内容读不出来（不是缺货、也不是被禁），换别的容器或散装。
        return text::keyed("shop.trade.sign.bundle_product");
    case ContainerError::AlreadyBound:
        return text::keyed("shop.trade.sign.chest_taken");
    case ContainerError::None:
        break;
    }
    return text::keyed("shop.trade.sign.chest_missing");
}

SignEditResult planSignEdit(
    BlockSource&      region,
    BlockPos const&   signPos,
    ServerPlayer&     editor,
    std::string_view  rawText,
    ShopConfig const& config
) {
    auto const signConfig = toSignConfig(config);

    // 两道闸只对「正面看起来已经是商店牌」的牌子生效：普通告示牌一个字都不能碰
    // （DESIGN §5.1.1），所以先看改前的文本。
    //
    // 注：Before 事件里 `SignBlockActor` 还是旧文本，包里才是新文本（DESIGN §6.1），
    // 所以这里看到的是「改之前的店」。判据是 `looksLikeShopSign`（像商店）而不是 `ok()`
    // （现在严格合法）：改配置、换管理员名、外部工具写脏数据，都不该让保护失效。
    SignOwnerHandoff handoff;
    std::string      existingOwnerName; // 已有商店的第 1 行该写谁（§11：写店主，不写点击者）

    if (auto* sign = signBlockActorAt(region, signPos); sign != nullptr) {
        auto const existing = parseShopSignFront(*sign, signConfig);
        if (existing.looksLikeShopSign()) {
            auto const owner = readSignOwner(*sign, existing);
            auto const actor = shopActorOf(owner.xuid, editor.getXuid(), editor.isOperator(), owner.adminShop);

            // 归属交接：改之前已经是商店就不该跟着编辑者走（除管理员店 ⇄ 普通店之外）。
            handoff.wasShop           = true;
            handoff.previousAdminShop = owner.adminShop;
            handoff.previousOwnerXuid = owner.xuid;
            existingOwnerName         = owner.displayName;

            // 两道闸：不是你的商店不放，上了蜡的牌谁都不放（DESIGN §9.2）。
            if (auto reason = signTextEditRejection(actor, sign->mIsWaxed, owner.displayName, owner.adminShop)) {
                return {.outcome = SignEditOutcome::Rejected, .message = std::move(*reason)};
            }
        }
    }

    auto const parsed = parseShopSignText(rawText, editor.isOperator(), signConfig);

    if (!parsed.looksLikeShopSign()) {
        // 普通告示牌：不读容器、不改文本、不打掉。
        return {};
    }
    if (!parsed.ok()) {
        return {.outcome = SignEditOutcome::Rejected, .message = messageFor(parsed.error, config.maxShopAmount)};
    }

    ContainerLookupOptions const options{
        config.shopContainers,
        signConfig,
        // 禁售只管玩家开的店：管理员店传 `nullptr`（服主自己摆的货架，卖什么由他定）。
        // 这里已经知道这块牌会变成什么店（`parsed.spec->adminShop`），所以判据与牌面同源。
        parsed.spec->adminShop ? nullptr : &currentBanList(),
    };
    auto const lookup = inspectAttachedContainer(region, signPos, options);
    if (!lookup.ok()) {
        // 这一次没成，牌的文本不会被改，归属也不该变（不交交接）。
        return {.outcome = SignEditOutcome::Rejected, .message = messageFor(lookup.error)};
    }

    // 第 1 行由服务器说了算：管理员店写配置名；已经属于别人的商店就写原店主的名字
    // （OP 代改不改归属，DESIGN §11）；新建才写本次编辑者。写谁与归属写谁同源
    // （都问 `keepsPreviousOwner`），不会出现“第 1 行是 A、店主是 B”的错位。
    std::string ownerName = config.adminShopName;
    if (!parsed.spec->adminShop) {
        bool const keepPrevious = handoff.keepsPreviousOwner(/*newAdminShop=*/false);
        ownerName               = keepPrevious && !existingOwnerName.empty() ? existingOwnerName : editor.getNameTag();
    }

    auto const formatted = formatShopSign(*parsed.spec, ownerName, lookup.itemName);
    return {
        .outcome        = SignEditOutcome::Normalized,
        .normalizedText = joinSignText(formatted),
        .ownerHandoff   = handoff,
    };
}

} // namespace ss::shop
