#include "shop/SignProtection.h"

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

/// 斧头（除蜡）。金斧头在基岩版是 `golden_axe`；铜制工具一并列上，免得新版本漏判。
/// 不按 `_axe` 后缀判：「pickaxe」（镐）也以 `axe` 结尾。
constexpr std::array<std::string_view, 7> kAxeNames{
    "woodenaxe",
    "stoneaxe",
    "copperaxe",
    "goldenaxe",
    "ironaxe",
    "diamondaxe",
    "netheriteaxe",
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
constexpr std::string_view kOwnerOnly = "只有店主（或 OP）能";

/// 「谁能」这半句按身份换：有主的牌说店主，没记下店主的牌只能找 OP（§9.2 的表）。
std::string_view allowedActorPhrase(ShopActor actor) {
    return actor == ShopActor::NoRecordedOwner ? std::string_view{"只有 OP 能"} : kOwnerOnly;
}

/// 被拦下的是牌子上的什么。
std::string_view touchSubject(SignTouch touch) {
    switch (touch) {
    case SignTouch::Dye:
        return "这块商店牌的颜色";
    case SignTouch::InkSac:
    case SignTouch::GlowInkSac:
        return "这块商店牌的文字样式";
    case SignTouch::HoneyComb:
    case SignTouch::Axe:
    case SignTouch::None:
        break;
    }
    return "这块商店牌";
}

/// 要做什么（GLOSSARY 里的「上蜡 / 除蜡」原样用）。
std::string_view touchAction(SignTouch touch) {
    switch (touch) {
    case SignTouch::Dye:
    case SignTouch::InkSac:
    case SignTouch::GlowInkSac:
        return "改";
    case SignTouch::HoneyComb:
        return "上蜡";
    case SignTouch::Axe:
        return "除蜡";
    case SignTouch::None:
        break;
    }
    return "动"; // 不可达的兜底：`None` 不会走到 Block（见 `planSignTouch`）
}

/// 「<主体><谁能><动作>」——一句话只在这里拼。
std::string onlyOwnerMessage(ShopActor actor, SignTouch touch) {
    std::string sentence{touchSubject(touch)};
    sentence.append(allowedActorPhrase(actor));
    sentence.append(touchAction(touch));
    return sentence;
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

std::optional<std::string>
signTextEditRejection(ShopActor actor, bool waxed, std::string const& ownerName, bool adminShop) {
    if (!mayEditShopSignText(actor)) {
        return ownerOnlyEditMessage(ownerName, adminShop);
    }
    if (waxed) {
        return waxedSignEditMessage();
    }
    return std::nullopt;
}

std::string ownerOnlyEditMessage(std::string const& ownerName, bool adminShop) {
    if (adminShop) {
        return "管理员商店只有 OP 能改"; // §11：管理员商店的管理权只给 OP
    }
    if (ownerName.empty()) {
        return "这块商店牌不是你的，" + std::string{kOwnerOnly} + "改";
    }
    return "这块商店牌是 " + ownerName + " 的，" + std::string{kOwnerOnly} + "改";
}

std::string waxedSignEditMessage() { return "这块商店牌上了蜡，先拿斧头除蜡才能改"; }

std::string shopBoxMissingMessage() { return "这家店暂时用不了（商店箱不在了），牌子还不能改"; }

std::string shopInvalidMessage() { return "这块商店牌的写法现在不算有效商店，牌子还不能改"; }

SignTouch classifySignTouch(std::string_view itemTypeName) {
    auto const name = normalizeItemName(itemTypeName);
    if (name.empty()) {
        return SignTouch::None;
    }

    if (contains(kDyeNames, name)) {
        return SignTouch::Dye;
    }
    if (contains(kAxeNames, name)) {
        return SignTouch::Axe;
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

    // 形状兜底：新版本加了颜色或材质时不至于静默放行。裸名（`dye` / `axe`）不算：
    // 要求 `size() >` 前缀，避免把「空的类别名」当成一类物品。
    auto const endsWith = [&name](std::string_view suffix) {
        return name.size() > suffix.size() && name.ends_with(suffix);
    };
    if (endsWith("pickaxe")) {
        return SignTouch::None; // 镐也是以 axe 结尾，但它不改牌子
    }
    if (endsWith("dye")) {
        return SignTouch::Dye;
    }
    if (endsWith("axe")) {
        return SignTouch::Axe;
    }
    return SignTouch::None;
}

SignTouchDecision planSignTouch(SignTouchContext const& context) {
    // 斧头对没上蜡的牌什么都不改，那一下就是一次普通点击（上蜡与否不影响商店语义）：
    // 不把它当成「除蜡操作」，所以谁点都一样。
    bool const plainClick = context.touch == SignTouch::None || (context.touch == SignTouch::Axe && !context.waxed);

    if (plainClick) {
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
        // 上蜡的牌原版也不给编辑，这里先说清原因。
        if (context.waxed) {
            return {SignTouchAction::Block, waxedSignEditMessage()};
        }
        return {SignTouchAction::PassThrough, {}};
    }

    // 物品类操作与看哪一面无关：上蜡/除蜡改的是整块牌子，染色与墨囊也不该由外人来按。
    // 到这里只可能是「真的有东西要改」：染料、墨囊、荧光墨囊、蜜蜡，以及上了蜡的斧头。
    if (!canManageShop(context.actor)) {
        return {SignTouchAction::Block, onlyOwnerMessage(context.actor, context.touch)};
    }
    // 店主与 OP：染色、墨囊、上蜡、除蜡都交给原版处理。
    return {SignTouchAction::PassThrough, {}};
}

} // namespace ss::shop
