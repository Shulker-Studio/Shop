#include "core/shop/ShopReader.h"

#include "core/sign/SignEditPlan.h"

#include "core/trade/TradePricing.h"
#include "core/trade/UnitCount.h"
#include "core/container/ContainerFinder.h"
#include "core/shop/ItemBanList.h"
#include "core/shop/SignOwner.h"
#include "core/sign/ShopSign.h"
#include "core/sign/SignProtection.h"
#include "util/i18n/Reason.h"
#include "util/i18n/Text.h"
#include "Shop.h"

#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/actor/SignBlockActor.h"
#include "mc/world/level/block/actor/SignTextSide.h"

#include <algorithm>
#include <limits>

namespace ss::shop {

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

} // namespace ss::shop
