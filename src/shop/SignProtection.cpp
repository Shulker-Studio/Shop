#include "shop/SignProtection.h"
#include "shop/Text.h"

#include <array>
#include <cctype>
#include <cstddef>

namespace ss::shop {
namespace {

/// 归一化：小写、去掉命名空间前缀（最后一个 `:` 之前的部分）、只留字母与数字。
///
/// `minecraft:red_dye`、`Red Dye`、`RED_DYE` 都变成 `reddye`。这样无论引擎给的是
/// 类型名还是展示名，表都只维护一份。
std::string normalizeItemName(std::string_view name) {
    auto const colon = name.rfind(':');
    if (colon != std::string_view::npos) {
        name.remove_prefix(colon + 1);
    }

    std::string normalized;
    normalized.reserve(name.size());
    for (char const ch : name) {
        auto const uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) != 0) {
            normalized.push_back(static_cast<char>(std::tolower(uch)));
        }
    }
    return normalized;
}

/// 16 种染料（基岩版 `minecraft:<颜色>_dye`）。骨粉、青金石这些不算染料。
constexpr std::array<std::string_view, 16> kDyeNames{
    "blackdye",
    "bluedye",
    "browndye",
    "cyandye",
    "graydye",
    "greendye",
    "lightbluedye",
    "lightgraydye",
    "limedye",
    "magentadye",
    "orangedye",
    "pinkdye",
    "purpledye",
    "reddye",
    "whitedye",
    "yellowdye",
};

template <std::size_t N>
bool contains(std::array<std::string_view, N> const& table, std::string const& name) {
    for (auto const candidate : table) {
        if (candidate == name) {
            return true;
        }
    }
    return false;
}

/// 「只有店主（或 OP）能 ……」这个句式只写一次：前面接主体、后面接动作。
constexpr std::string_view kOwnerOnly = "shop.protection.actor.owner";

/// 「谁能」这半句按身份换：有主的牌说店主，没记下店主的牌只能找 OP（§9.2 的表）。
std::string_view allowedActorPhrase(ShopActor actor) {
    return actor == ShopActor::NoRecordedOwner ? std::string_view{"shop.protection.actor.op"} : kOwnerOnly;
}

/// 被拦下的是牌子上的什么。
std::string_view touchSubject(SignTouch touch) {
    switch (touch) {
    case SignTouch::Dye:
        return "shop.protection.subject.dye";
    case SignTouch::InkSac:
    case SignTouch::GlowInkSac:
        return "shop.protection.subject.style";
    case SignTouch::HoneyComb:
    case SignTouch::None:
        break;
    }
    return "shop.protection.subject.sign";
}

/// 要做什么（GLOSSARY 里的「上蜡」原样用）。
std::string_view touchAction(SignTouch touch) {
    switch (touch) {
    case SignTouch::Dye:
    case SignTouch::InkSac:
    case SignTouch::GlowInkSac:
        return "shop.protection.action.change";
    case SignTouch::HoneyComb:
        return "shop.protection.action.wax";
    case SignTouch::None:
        break;
    }
    return "shop.protection.action.touch"; // 不可达的兜底：`None` 不会走到 Block（见 `planSignTouch`）
}

/// 「<主体><谁能><动作>」——一句话只在这里拼。
Text onlyOwnerMessage(ShopActor actor, SignTouch touch) {
    return text::sequence({
        text::keyed(std::string{touchSubject(touch)}),
        text::keyed(std::string{allowedActorPhrase(actor)}),
        text::keyed(std::string{touchAction(touch)}),
    });
}

} // namespace

ShopActor shopActorOf(std::string_view recordedOwnerXuid, std::string_view actorXuid, bool actorIsOp, bool adminShop) {
    if (actorIsOp) {
        return ShopActor::Operator; // DESIGN §11：OP 有全部权限，不再往下比 XUID
    }
    if (adminShop) {
        // 管理员商店的管理权只给 OP（§11）：牌上记的 XUID 只是当时的创建者。
        return ShopActor::Other;
    }
    if (recordedOwnerXuid.empty()) {
        return ShopActor::NoRecordedOwner;
    }
    if (recordedOwnerXuid == actorXuid) {
        return ShopActor::Owner;
    }
    return ShopActor::Other;
}

bool canManageShop(ShopActor actor) {
    // 记录不到店主时不把管理入口发给所有人：那是 fail-closed 的一侧。
    return actor == ShopActor::Owner || actor == ShopActor::Operator;
}

bool mayEditShopSignText(ShopActor actor) {
    // 只有游客被挡；`NoRecordedOwner` 是 fail-open 的一侧（DESIGN §9.2）。
    return actor != ShopActor::Other;
}

std::optional<Text>
signTextEditRejection(ShopActor actor, bool waxed, std::string const& ownerName, bool adminShop) {
    if (!mayEditShopSignText(actor)) {
        return ownerOnlyEditMessage(ownerName, adminShop);
    }
    if (waxed) {
        return waxedSignEditMessage();
    }
    return std::nullopt;
}

Text ownerOnlyEditMessage(std::string const& ownerName, bool adminShop) {
    if (adminShop) {
        // §11：管理员商店的管理权只给 OP
        return text::keyed("shop.protection.admin_only");
    }
    if (ownerName.empty()) {
        return text::keyed("shop.protection.owner_only_anonymous");
    }
    return text::keyed("shop.protection.owner_only", Tone::Default, {text::lit(ownerName)});
}

Text waxedSignEditMessage() {
    // 必须说清「原版去不掉蜡」：不然玩家会一直去试斧头（老版本的习惯），
    // 而斧头对告示牌只当普通点击用。
    return text::keyed("shop.protection.waxed_edit");
}

Text waxedSignTouchMessage() { return text::keyed("shop.protection.waxed_touch"); }

Text shopBoxMissingMessage() { return text::keyed("shop.protection.chest_missing"); }

Text shopInvalidMessage() { return text::keyed("shop.protection.invalid_sign"); }

SignTouch classifySignTouch(std::string_view itemTypeName) {
    auto const name = normalizeItemName(itemTypeName);
    if (name.empty()) {
        return SignTouch::None;
    }

    if (contains(kDyeNames, name)) {
        return SignTouch::Dye;
    }
    if (name == "inksac") {
        return SignTouch::InkSac;
    }
    if (name == "glowinksac") {
        return SignTouch::GlowInkSac;
    }
    if (name == "honeycomb") {
        return SignTouch::HoneyComb;
    }

    // 形状兜底：新版本加了颜色时不至于静默放行。裸名（`dye`）不算：
    // 要求 `size() >` 前缀，避免把「空的类别名」当成一类物品。
    auto const endsWith = [&name](std::string_view suffix) {
        return name.size() > suffix.size() && name.ends_with(suffix);
    };
    if (endsWith("dye")) {
        return SignTouch::Dye;
    }
    // 斧头 / 镐 / 锹 都落不到这里：原版不能用斧头给告示牌除蜡（见 `SignTouch` 的注释），
    // 拿它们点牌就是普通点击。
    return SignTouch::None;
}

SignTouchDecision planSignTouch(SignTouchContext const& context) {
    // 普通点击 = 手里没有会改牌子的东西。斧头也在这里：原版没有「斧头给告示牌除蜡」这回事，
    // 所以拿斧头点牌与空手点牌完全一样（正面弹表单、背面放行）。
    if (context.touch == SignTouch::None) {
        // 背面是布告板：不看内容、不改格式，原版编辑界面照弹（DESIGN §5.5）。
        if (!context.facingFront) {
            return {SignTouchAction::PassThrough, {}};
        }
        // 普通点击：弹表单。潜行右键才是「想改文本」的编辑入口。
        if (!context.sneaking) {
            return {SignTouchAction::TakeOver, {}};
        }
        // 谁能这样进编辑界面，问的是与提交阶段同一个判据（游客拦下：潜行不是后门）。
        if (!mayEditShopSignText(context.actor)) {
            return {SignTouchAction::TakeOver, {}};
        }
        // 上蜡的牌原版也不给编辑（而且没有反向操作），先把原因说清：连店主也只能拆牌重立。
        if (context.waxed) {
            return {SignTouchAction::Block, waxedSignEditMessage()};
        }
        return {SignTouchAction::PassThrough, {}};
    }

    // 染料 / 墨囊 / 荧光墨囊 / 蜜蜡：改的是整块牌子，与看哪一面无关。
    // 已经上蜡的牌，原版对这四样一律拒绝——所以先说「上了蜡」，不拿权限说事
    // （否则店主会以为「是我没权限」，其实谁都改不了）。
    if (context.waxed) {
        return {SignTouchAction::Block, waxedSignTouchMessage()};
    }
    if (!canManageShop(context.actor)) {
        return {SignTouchAction::Block, onlyOwnerMessage(context.actor, context.touch)};
    }
    // 店主与 OP：染色、墨囊、上蜡都交给原版处理。
    return {SignTouchAction::PassThrough, {}};
}

} // namespace ss::shop
