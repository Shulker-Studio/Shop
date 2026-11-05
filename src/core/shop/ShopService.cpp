#include "core/shop/ShopService.h"
#include "util/i18n/Reason.h"

#include "core/container/ContainerFinder.h"
#include "core/economy/Service.h"
#include "Shop.h"
#include "core/shop/SignOwner.h"
#include "core/sign/SignProtection.h"
#include "core/trade/TradePricing.h"

#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include <algorithm>
#include <limits>

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

namespace {

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

} // namespace

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

std::optional<ShopSnapshot> readShop(BlockSource& region, BlockPos const& signPos, ShopConfig const& config) {
    auto* sign = signBlockActorAt(region, signPos);
    if (sign == nullptr) {
        return std::nullopt;
    }

    auto const signConfig = toSignConfig(config);
    auto const parsed     = parseShopSignFront(*sign, signConfig);
    if (!parsed.ok()) {
        return std::nullopt; // 普通牌、非法牌
    }

    // 店主先读：它能告诉下面“这是不是管理员店”，而禁售只管普通店。
    SignOwner const owner = readSignOwner(*sign, parsed);

    ContainerLookupOptions const options{
        config.shopContainers,
        signConfig,
        owner.adminShop ? nullptr : &currentBanList(),
    };
    auto const lookup = inspectAttachedContainer(region, signPos, options);
    if (!lookup.ref) {
        return std::nullopt; // 附着格不是可用容器：这块牌现在还不是商店
    }

    ShopInfoData info;
    info.ownerName = owner.displayName;
    info.adminShop = owner.adminShop;
    // 卖光了就没有商品可读，退回牌面第 4 行（规范化时写的就是商品名）。
    info.itemName = lookup.itemName.empty() ? splitSignText(sign->getMessage(SignTextSide::Front))[SignLine::ItemLine]
                                            : lookup.itemName;
    // 与 `itemName` 分开两位：上面那行在箱子空时会退回牌面原文，所以 `itemName` 非空**不代表**
    // 商品还在箱子里。管理表单的「更新商品名」按 `hasProduct` 决定给不给。
    info.hasProduct  = lookup.hasProduct;
    info.boxContents = lookup.boxContents;
    info.amountPerUnit = parsed.spec->amount;
    info.buyPrice      = parsed.spec->buyPrice;
    info.sellPrice     = parsed.spec->sellPrice;
    // 停业是牌面上的状态（第 3 行末尾的 `:x`）：牌子一读就带过来，不靠内存标记。
    info.closed        = parsed.spec->closed;

    // 箱子这一侧到底能不能卖：三种“不能”的状态由纯逻辑判（`boxStockStateOf`），
    // 这里只把结论换成数字与一句话。顺序固定：混装 > 空箱 > 只剩样品。
    //
    // 可成交份数用**可卖件数**（样品槽永远不出售），不是箱子里的总件数——两者在“只放了一份”
    // 的店里差得很明显：总件数说“有 1 份”，可卖件数说“0 份”。
    //
    // 管理员店不参与这条：它的箱子是模板箱，可成交份数无限（spec D2），也没有“只剩样品”这回事。
    BoxStockState const boxState = boxStockStateOf(
        lookup.error == ContainerError::Empty,
        lookup.error == ContainerError::MixedContainerContents,
        info.adminShop || lookup.saleableItemCount > 0
    );
    // 件数 → 份数的除法只有 `unitsFromItems` 一处，商店箱的货与背包容量共用。
    info.stockUnits = info.adminShop
                        ? UnitCount::makeUnlimited()
                        : UnitCount::makeFinite(unitsFromItems(
                              static_cast<int>(std::min<std::int64_t>(
                                  lookup.saleableItemCount,
                                  std::numeric_limits<int>::max()
                              )),
                              info.amountPerUnit
                          ));
    // 货币符号只从配置来：这里不留第二个默认值。
    info.currencySymbol = config.economy.currencySymbol;

    // 箱子全空：既没东西可卖，也看不出这家店收什么（商品权威是第一个非空槽位，DESIGN §5.3）。
    // 牌子还是商店（不会被当成普通牌），只是现在什么都交易不了，原因写在正文里。
    // 管理员店的说法不同：它的箱子是模板，空着 = **还没设商品**（票 03），不是“卖光了”。
    // “只剩样品”与“盒内混装”也都不是“没货”：前者要补货、后者得整理盒子。
    // 两者走两个通道：混装拦按钮（两个方向都用不了），只剩样品只作说明（店收购方向还能用）。
    // 禁售不走 `BoxStockState`：它不是“数量”能描述的状态，而且这里的可卖件数为 0，
    // 会被算成 `OnlySampleLeft`，那句“只剩样品了”就把店主引到错的修理方向去了。
    if (lookup.error == ContainerError::BannedItem) {
        info.unavailableReason = bannedItemReason();
    } else if (lookup.error == ContainerError::BundleProduct) {
        info.unavailableReason = bundleProductReason();
    } else if (lookup.error == ContainerError::AlreadyBound) {
        // 这块牌贴在**别人已经占着的**箱子上（同一容器挂了两块牌）：箱子里的货不归它，
        // 所以既不能说“没货”，更不能说“只剩样品”——那会把店主指到一条不存在的修理路子上。
        info.unavailableReason = messageFor(ContainerError::AlreadyBound);
    } else {
        info.unavailableReason = unavailableReasonForStockState(boxState, info.adminShop);
        info.stockNote         = stockNoteForStockState(boxState);
    }

    return ShopSnapshot{.info = info, .ownerXuid = owner.xuid};
}

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
