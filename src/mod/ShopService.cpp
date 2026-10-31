#include "mod/ShopService.h"

#include "mod/ContainerFinderBds.h"
#include "mod/EconomyService.h"
#include "mod/ShopOwner.h"
#include "shop/SignProtection.h"
#include "shop/TradePricing.h"

#include "mc/server/ServerPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include <algorithm>
#include <limits>

namespace ss::shop {

std::string messageFor(ShopSignError error, int maxShopAmount) {
    switch (error) {
    case ShopSignError::EmptyOwnerName:
        return "第 1 行不能空着（服务器会把它换成你的名字，随便写点什么）";
    case ShopSignError::BadAmount:
        return "一份的件数要写 1 到 " + std::to_string(maxShopAmount) + " 之间的整数";
    case ShopSignError::BadPrice:
        return "价格写法不对：写 B <买入价> : S <卖出价>，至少写一个";
    case ShopSignError::SellPriceAboveBuy:
        return "卖出价不能高于买入价";
    case ShopSignError::None:
    case ShopSignError::NotAShopSign:
        break;
    }
    return "商店牌写得不对";
}

namespace {

std::string messageFor(ContainerError error) {
    switch (error) {
    case ContainerError::NotAContainer:
    case ContainerError::UnknownSignShape:
        // 立在箱子旁边的地上、贴在箱子旁边的墙上、侧挂的悬挂牌都走这里。
        return "牌子要贴在或立在箱子上";
    case ContainerError::NotAllowedContainer:
        return "这种容器不能当商店用（服主可配置可用的容器）";
    case ContainerError::Empty:
        return "请先在箱子里放商品";
    case ContainerError::AlreadyBound:
        return "这个箱子已经有商店了";
    case ContainerError::None:
        break;
    }
    return "找不到商店的箱子";
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

    // 两道闸只对「正面已经有主的商店牌」生效：普通告示牌一个字都不能碰
    // （DESIGN §5.1.1），所以先看改前的文本。
    //
    // 注：Before 事件里 `SignBlockActor` 还是旧文本，包里才是新文本（DESIGN §6.1），
    // 所以这里看到的是「改之前的店」。判据是 `looksLikeShopSign`（像商店）而不是 `ok()`
    // （现在严格合法）：改配置、换管理员名、外部工具写脏数据，都不该让保护失效。
    SignOwnerHandoff handoff;
    std::string      existingOwnerName; // 已有商店的第 1 行该写谁（§11：写店主，不写点击者）
    bool             existingAdminShop = false;

    if (auto* sign = signBlockActorAt(region, signPos); sign != nullptr) {
        auto const existing = parseShopSignFront(*sign, signConfig);
        if (existing.looksLikeShopSign()) {
            auto const owner = readShopOwner(*sign, existing);
            auto const actor = shopActorOf(owner.xuid, editor.getXuid(), editor.isOperator());

            // 归属交接：改之前已经是商店（哪怕没有容器），归属就不该跟着编辑者走。
            handoff.wasShop           = true;
            handoff.previousOwnerXuid = owner.xuid;
            existingOwnerName         = owner.displayName;
            existingAdminShop         = owner.adminShop;

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

    ContainerLookupOptions const options{config.shopContainers, signConfig};
    auto const                   lookup = inspectAttachedContainer(region, signPos, options);
    if (!lookup.ok()) {
        // 这一次没成，牌的文本不会被改，归属也不该变（不交交接）。
        return {.outcome = SignEditOutcome::Rejected, .message = messageFor(lookup.error)};
    }

    // 第 1 行由服务器说了算：管理员店写配置名；已经是别人的商店就写原店主的名字
    // （OP 代改不改归属，DESIGN §11）；新建才写本次编辑者。
    std::string ownerName = config.adminShopName;
    if (!parsed.spec->adminShop) {
        bool const keepExistingOwner = handoff.wasShop && !existingOwnerName.empty() && !existingAdminShop;
        ownerName                    = keepExistingOwner ? existingOwnerName : editor.getNameTag();
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
    auto const parsed     = parseShopSignText(sign->getMessage(SignTextSide::Front), /*editorIsOp=*/true, signConfig);
    if (!parsed.ok()) {
        return std::nullopt; // 普通牌、非法牌
    }

    ContainerLookupOptions const options{config.shopContainers, signConfig};
    auto const                   lookup = inspectAttachedContainer(region, signPos, options);
    if (!lookup.ref) {
        return std::nullopt; // 附着格不是可用容器：这块牌现在还不是商店
    }

    ShopOwner const owner = readShopOwner(*sign, parsed);

    ShopInfoData info;
    info.ownerName = owner.displayName;
    info.adminShop = owner.adminShop;
    // 卖光了就没有商品可读，退回牌面第 4 行（规范化时写的就是商品名）。
    info.itemName = lookup.itemName.empty() ? splitSignText(sign->getMessage(SignTextSide::Front))[SignLine::ItemLine]
                                            : lookup.itemName;
    info.amountPerUnit = parsed.spec->amount;
    info.buyPrice      = parsed.spec->buyPrice;
    info.sellPrice     = parsed.spec->sellPrice;
    // 件数 → 份数的除法只有 `unitsFromItems` 一处，商店箱的货与背包容量共用。
    info.stockUnits = unitsFromItems(
        static_cast<int>(std::min<std::int64_t>(lookup.itemCount, std::numeric_limits<int>::max())),
        info.amountPerUnit
    );
    // 货币符号只从配置来：这里不留第二个默认值。
    info.currencySymbol = config.economy.currencySymbol;

    // 箱子全空：既没东西可卖，也看不出这家店收什么（商品权威是第一个非空槽位，DESIGN §5.3）。
    // 牌子还是商店（不会被当成普通牌），只是现在什么都交易不了，原因写在正文里。
    if (lookup.error == ContainerError::Empty) {
        info.unavailableReason = emptyBoxReason();
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
    ShopOwner const owner = readShopOwner(*sign, parsed);
    ShopActor const actor = shopActorOf(owner.xuid, viewer.getXuid(), viewer.isOperator());

    ShopInteraction interaction{
        .actor       = actor,
        .role        = canManageShop(actor) ? ShopFormRole::Manager : ShopFormRole::Visitor,
        .waxed       = sign->mIsWaxed,
        .facingFront = sign->getSideFacingPlayer(viewer) == SignTextSide::Front,
    };

    // 牌子像商店但现在能不能交易，看附着格（DESIGN §9.3）。不能交易也照样返回：
    // 牌面保护不看容器（§9.2），只是没表单可弹。
    auto shop = readShop(region, signPos, config);
    if (!shop) {
        return interaction;
    }

    // 背面是布告板：那一面不弹表单，也就不必问余额与「能不能交易」（DESIGN §5.5）。
    if (interaction.facingFront) {
        // 余额与「能不能交易」都要问后端，只问一次。
        // `unavailableReason` 可能已经被 `readShop` 填过（箱子空），那个原因更具体，不覆盖它。
        auto& provider = economyBackend();
        if (!provider.available()) {
            // 没有可用后端：余额不编造数字（DESIGN §10.1），买卖按钮也不给。
            if (shop->info.unavailableReason.empty()) {
                shop->info.unavailableReason = noEconomyReason();
            }
        } else {
            // 读不到就留空：正文写「暂时读不到」，不猜一个数字（DESIGN §10.1）。
            shop->info.balance = provider.get(viewer.getXuid());
            // 店主收不了钱时提前把买卖按钮藏起来（DESIGN §8.5）：只有不支持离线记账的
            // 后端（记分板）才会碰到这个情况，支持离线的后端根本不用查店主在不在线。
            // 管理员店不受这条约束：它的资金端是系统账户（§8.5 表格的「同左」）。
            if (!shop->info.adminShop && shop->info.unavailableReason.empty() && !provider.supportsOffline()
                && onlinePlayer(shop->ownerXuid) == nullptr) {
                shop->info.unavailableReason = ownerOfflineReason();
            }
        }
    }

    interaction.shop = std::move(shop);
    return interaction;
}

} // namespace ss::shop
