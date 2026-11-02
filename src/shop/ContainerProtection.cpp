#include "shop/ContainerProtection.h"

#include <string>
#include <string_view>

namespace ss::shop {
namespace {

/// 一种操作的说法。三种操作的全部文案差异都从这张表来：加一个取值时只改这里，
/// 不会出现「subject 改了、verb 忘了」的半句话。存的都是词条 id，渲染在 `Text` 里做。
struct TouchWords {
    Text subject; ///< 「商店箱」/「商店牌」
    Text verb;    ///< 「打开」/「拆掉」
    /// 「无主牌」提示里第二次提到这件事时的短称——那句话开头已经说过「这块商店牌」。
    Text object;
};

TouchWords wordsOf(ShopBlockTouch touch) {
    switch (touch) {
    case ShopBlockTouch::OpenChest:
        return {
            text::keyed("shop.protection.noun.chest"),
            text::keyed("shop.protection.verb.open"),
            text::keyed("shop.protection.pron.chest")
        };
    case ShopBlockTouch::BreakChest:
        return {
            text::keyed("shop.protection.noun.chest"),
            text::keyed("shop.protection.verb.break"),
            text::keyed("shop.protection.pron.chest")
        };
    case ShopBlockTouch::BreakSign:
        return {
            text::keyed("shop.protection.noun.sign"),
            text::keyed("shop.protection.verb.break"),
            text::keyed("shop.protection.pron.sign")
        };
    case ShopBlockTouch::BreakSignSupport:
        // 拆依附方块时说“这块方块上挂着……商店牌”；主语是牌子，宾语是那块方块本身。
        return {
            text::keyed("shop.protection.noun.sign"),
            text::keyed("shop.protection.verb.break_support"),
            text::keyed("shop.protection.pron.block")
        };
    }
    return {
        text::keyed("shop.protection.noun.chest"),
        text::keyed("shop.protection.verb.open"),
        text::keyed("shop.protection.pron.chest")
    };
}

/// 非店主被拦下的提示。管理员商店单独一句：它的管理权只给 OP（DESIGN §11），
/// 写「只有店主能…」会让人去找一个不存在的人。
Text ownerOnlyMessage(ShopBlockTouch touch, std::string const& ownerName, bool adminShop) {
    auto const words = wordsOf(touch);

    if (adminShop) {
        return text::keyed("shop.protection.container.admin_only", Tone::Default, {words.subject, words.verb});
    }
    if (ownerName.empty()) {
        // 牌上没记名字但记了 XUID（或 PlayerInfo 查不到）：不拼「是  的商店箱」这种半句话。
        return text::keyed("shop.protection.container.other_owner", Tone::Default, {words.subject, words.verb});
    }
    return text::keyed(
        "shop.protection.container.owner_named",
        Tone::Default,
        {text::lit(ownerName), words.subject, words.verb}
    );
}

/// 没记下店主 XUID 的牌（DESIGN §6.5 第 3 条）：谁都不是店主，只给 OP。
Text noRecordedOwnerMessage(ShopBlockTouch touch) {
    auto const words = wordsOf(touch);
    return text::keyed(
        "shop.protection.container.no_owner",
        Tone::Default,
        {words.verb, words.object}
    );
}

/// 店主被 `allowOwnerOpenChest = false` 挡下：提示要点出服主的开关，
/// 否则玩家会以为是自己没有权限。动词与宾语仍然从 `wordsOf` 来，不另写一份。
Text ownerOpenDisabledMessage() {
    auto const words = wordsOf(ShopBlockTouch::OpenChest);
    return text::keyed(
        "shop.protection.container.owner_open_disabled",
        Tone::Default,
        {words.verb, words.object}
    );
}

/// 拆「商店牌依附的那一格」（DESIGN §6.2 H3e）：玩家以为自己在拆一块普通方块，
/// 不知道牌子会跟着掉、商店会没，所以这一句必须把后果说出来。
/// 主语与动词仍然从 `wordsOf` 取，保持「加一个取值只改那张表」的口径。
Text supportBlockMessage(ShopActor actor, std::string const& ownerName, bool adminShop) {
    auto const words = wordsOf(ShopBlockTouch::BreakSignSupport);

    // 管理员商店与无主牌都只给 OP（§9.2 / §11），写“店主”会让人去找一个不存在的人。
    // 这两条各自有整句模板，剩下的两种身份共用「只有店主 (或 OP) 能」那一句。
    if (adminShop) {
        return text::keyed(
            "shop.protection.container.support.admin",
            Tone::Default,
            {words.subject, words.verb, words.object}
        );
    }
    if (actor == ShopActor::NoRecordedOwner) {
        return text::keyed(
            "shop.protection.container.support.no_owner",
            Tone::Default,
            {words.subject, words.verb, words.object}
        );
    }
    if (!ownerName.empty()) {
        return text::keyed(
            "shop.protection.container.support.named",
            Tone::Default,
            {text::lit(ownerName), words.subject, words.verb, words.object}
        );
    }
    return text::keyed(
        "shop.protection.container.support.other",
        Tone::Default,
        {words.subject, words.verb, words.object}
    );
}

} // namespace

ShopBlockDecision planShopBlockTouch(ShopBlockTouchContext const& context) {
    // 「管理身份」只有一处口径：店主与 OP（`canManageShop`，§9.2 / §11）。
    // 开箱多一道配置闸，所以它不走这个判据的短路。
    switch (context.touch) {
    case ShopBlockTouch::OpenChest:
        if (context.actor == ShopActor::Operator) {
            return {};
        }
        if (context.actor == ShopActor::Owner) {
            return context.allowOwnerOpenChest ? ShopBlockDecision{}
                                               : ShopBlockDecision{false, ownerOpenDisabledMessage()};
        }
        break;
    case ShopBlockTouch::BreakChest:
    case ShopBlockTouch::BreakSign:
    case ShopBlockTouch::BreakSignSupport:
        if (canManageShop(context.actor)) {
            return {};
        }
        break;
    }

    if (context.touch == ShopBlockTouch::BreakSignSupport) {
        return {false, supportBlockMessage(context.actor, context.ownerName, context.adminShop)};
    }
    if (context.actor == ShopActor::NoRecordedOwner) {
        return {false, noRecordedOwnerMessage(context.touch)};
    }
    return {false, ownerOnlyMessage(context.touch, context.ownerName, context.adminShop)};
}

} // namespace ss::shop
