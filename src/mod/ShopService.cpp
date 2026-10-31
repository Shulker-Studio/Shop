#include "mod/ShopService.h"

#include "mod/ContainerFinderBds.h"
#include "mod/ShopOwner.h"
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
    auto const parsed     = parseShopSignText(rawText, editor.isOperator(), signConfig);

    if (!parsed.looksLikeShopSign()) {
        // 普通告示牌：不读容器、不改文本、不打掉。
        return {};
    }
    if (!parsed.ok()) {
        return {.outcome = SignEditOutcome::Rejected, .message = messageFor(parsed.error, config.maxShopAmount)};
    }

    ContainerLookupOptions const options{config.shopContainers, signConfig};
    auto const                  lookup = inspectAttachedContainer(region, signPos, options);
    if (!lookup.ok()) {
        return {.outcome = SignEditOutcome::Rejected, .message = messageFor(lookup.error)};
    }

    // 第 1 行由服务器说了算：管理员店写配置名，普通店写编辑者当前的名字。
    std::string ownerName = config.adminShopName;
    if (!parsed.spec->adminShop) {
        ownerName = editor.getNameTag();
    }

    auto const formatted = formatShopSign(*parsed.spec, ownerName, lookup.itemName);
    return {.outcome = SignEditOutcome::Normalized, .normalizedText = joinSignText(formatted)};
}

std::optional<ShopSnapshot> inspectShop(
    BlockSource&      region,
    BlockPos const&   signPos,
    Player&           viewer,
    ShopConfig const& config
) {
    auto* sign = signBlockActorAt(region, signPos);
    if (sign == nullptr) {
        return std::nullopt;
    }
    // 背面是布告板（DESIGN §5.5）：玩家看着背面时，原版编辑界面照常弹。
    if (sign->getSideFacingPlayer(viewer) != SignTextSide::Front) {
        return std::nullopt;
    }

    auto const signConfig = toSignConfig(config);
    auto const parsed     = parseShopSignText(sign->getMessage(SignTextSide::Front), /*editorIsOp=*/true, signConfig);
    if (!parsed.ok()) {
        return std::nullopt; // 普通牌、非法牌：一律交给原版
    }

    ContainerLookupOptions const options{config.shopContainers, signConfig};
    auto const                  lookup = inspectAttachedContainer(region, signPos, options);
    if (!lookup.ref) {
        return std::nullopt; // 附着格不是可用容器：这块牌现在还不是商店
    }

    ShopOwner const owner = readShopOwner(*sign, parsed);

    ShopInfoData info;
    info.ownerName     = owner.displayName;
    info.adminShop     = owner.adminShop;
    // 卖光了就没有商品可读，退回牌面第 4 行（规范化时写的就是商品名）。
    info.itemName      = lookup.itemName.empty()
                             ? splitSignText(sign->getMessage(SignTextSide::Front))[SignLine::ItemLine]
                             : lookup.itemName;
    info.amountPerUnit = parsed.spec->amount;
    info.buyPrice      = parsed.spec->buyPrice;
    info.sellPrice     = parsed.spec->sellPrice;
    info.stockUnits    = stockUnits(
        static_cast<int>(std::min<std::int64_t>(lookup.itemCount, std::numeric_limits<int>::max())),
        info.amountPerUnit
    );
    // 余额要等经济后端（10）；货币符号要等 `economy.currencySymbol` 进配置后再填。
    info.balance = std::nullopt;

    ShopSnapshot snapshot{info, ShopFormRole::Visitor};
    bool const   isOwner = !owner.xuid.empty() && owner.xuid == viewer.getXuid();
    if (viewer.isOperator() || isOwner) {
        snapshot.role = ShopFormRole::Manager;
    }
    return snapshot;
}

} // namespace ss::shop
