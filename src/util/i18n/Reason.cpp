#include "util/i18n/Reason.h"

#include "util/i18n/Text.h"

namespace ss::shop {
namespace {

/// 「N 份」或「无限」。
///
/// 无限**不编数字**：编出来的那个数会被玩家当成「还剩这么多」，而这正是票 01/02 要拆掉的
/// 两套真相。也**不解释为什么无限**——「无限」两个字说完了，再补一句「管理员店永远有货」
/// 只是把同一件事说第二遍。
Text countUnitsText(UnitCount const& stock) {
    if (auto const finite = stock.finiteUnits()) {
        return text::keyed("shop.form.units_count", Tone::Default, {text::lit(std::to_string(*finite))});
    }
    return text::keyed("shop.form.unlimited");
}

} // namespace

std::string moneyText(std::string const& symbol, std::int64_t amount) {
    if (symbol.empty()) {
        return std::to_string(amount);
    }
    return symbol + " " + std::to_string(amount);
}

Text stockUnitsText(UnitCount const& stock) { return countUnitsText(stock); }

Text boxRoomText(UnitCount const& stock) { return countUnitsText(stock); }

void appendLine(std::string& content, std::string const& line) {
    if (!content.empty()) {
        content.push_back('\n');
    }
    content.append(line);
}

Text ownerOfflineReason() { return text::keyed("shop.form.reason.owner_offline"); }

Text ownerFundsUnknownReason() { return text::keyed("shop.form.reason.owner_funds_unknown"); }

Text playerFundsUnknownReason() { return text::keyed("shop.form.reason.player_funds_unknown"); }

Text noEconomyReason() { return text::keyed("shop.form.reason.no_economy"); }

Text noSystemAccountReason() { return text::keyed("shop.form.reason.no_system_account"); }

Text emptyBoxReason() { return text::keyed("shop.form.reason.empty_box"); }

Text unavailableReasonForEmptyBox(bool adminShop) {
    if (!adminShop) {
        return emptyBoxReason();
    }
    // 管理员店的模板箱空着不是「卖光了」：它永远不会卖光，只是还没告诉它卖什么。
    // 写成缺货会让玩家一直在那儿等补货，而补货这件事对模板箱没有意义。
    return text::keyed("shop.form.reason.admin_no_sample");
}

Text onlySampleLeftReason() { return text::keyed("shop.form.reason.only_sample_left"); }

Text mixedContainerContentsReason() { return text::keyed("shop.form.reason.mixed_container_contents"); }

Text bannedItemReason() { return text::keyed("shop.form.reason.banned_item"); }

Text bundleProductReason() { return text::keyed("shop.form.reason.bundle_product"); }

Text unavailableReasonForStockState(BoxStockState state, bool adminShop) {
    switch (state) {
    case BoxStockState::MixedContents:
        // 两家店同文：管理员店的样品盒也得整理好才能当模板，这条不分店的性质。
        // 这个状态两个方向都不能用（商品的身份已经说不清了），所以它拦按钮。
        return mixedContainerContentsReason();
    case BoxStockState::Empty:
        return unavailableReasonForEmptyBox(adminShop);
    case BoxStockState::OnlySampleLeft:
        // 不拦按钮：店收购方向仍然可用，它的说明在 `stockNoteForStockState`。
        break;
    case BoxStockState::Saleable:
        break;
    }
    return {};
}

Text stockNoteForStockState(BoxStockState state) {
    return state == BoxStockState::OnlySampleLeft ? onlySampleLeftReason() : Text{};
}

Text taxChargeFailureReason(Text const& detail) {
    return text::keyed("shop.form.reason.tax_failed", Tone::Default, {detail});
}

Text taxBrokenReason(Text const& detail) {
    return text::keyed("shop.form.reason.tax_broken", Tone::Default, {detail});
}

} // namespace ss::shop
