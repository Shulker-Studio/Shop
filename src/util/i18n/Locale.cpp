#include "util/i18n/Locale.h"

#include "util/i18n/Message.h"
#include "util/i18n/Text.h"
#include "core/sign/ShopSign.h"
#include "Shop.h"

#include "fmt/format.h"
#include "ll/api/i18n/I18n.h"
#include "mc/world/actor/player/Player.h"

#include <array>
#include <filesystem>
#include <string_view>
#include <utility>

namespace ss::shop::ui {
namespace {

/// 语言文件是否真的载入成功。
///
/// 没载入时 `I18n::get` 对所有键都返回空，那与「没有这条翻译」无法区分，所以先在这里
/// 记一笔，省掉每条消息都去查一次空表。
bool gLoaded = false;

/// 词表不可用时的兜底文案。
///
/// 只有**两条**：`loadTexts()` 自己的启动日志。它们必须在「没有词表」的情况下也能读，
/// 所以不能只放在 `lang/*.json` 里——那时查表只会拿到 key id，比中文原文更难读。
///
/// 注意这不是「硬编码例外」：这两条日志照样走 `ui::msg` → `renderText` 的同一条通道，
/// 兜底表只是查表链的最后一环。`renderText` 与 `text::installLookup` 都经过 `lookupOrBoot`，
/// 所以词表在的时候用词表，不在的时候用这里。
constexpr std::array kBootMessages{
    std::pair{
        std::string_view{"shop.log.text.no_lang_dir"},
        std::string_view{"没有 lang/ 目录（{}）：玩家文本会显示成 key id，请重新安装本模组"}
    },
    std::pair{
        std::string_view{"shop.log.text.load_failed"},
        std::string_view{"载入 lang/ 失败（{}）：{}；玩家文本会显示成 key id，请重新安装本模组"}
    },
};

/// 查表：先问 `ll::i18n`，再问内置兜底表，都没有就返回空（由调用方决定怎么退化）。
std::string_view lookupOrBoot(std::string_view key, std::string_view locale) {
    auto const pattern = ll::i18n::getInstance().get(key, locale);
    if (!pattern.empty()) {
        return pattern;
    }
    for (auto const& [bootKey, bootText] : kBootMessages) {
        if (bootKey == key) {
            return bootText;
        }
    }
    return {};
}

} // namespace

void loadTexts() {
    // 纯逻辑层的表单文本走同一张表：把 `ll::i18n` 的查表注入进去。
    // 就算没有语言文件也注入——查不到条目时 `get` 返回空，`text::tr` 会退回中文原文。
    text::installLookup([](std::string_view key) -> std::string_view { return lookupOrBoot(key, {}); });
    text::installRenderer(&renderText);

    auto&                       logger = Shop::getInstance().getSelf().getLogger();
    std::filesystem::path const dir    = Shop::getInstance().getSelf().getLangDir();

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        // **这不是可选项**：结构化文本改造之后，代码里已经没有中文原文了，
        // 玩家看到的每一句话都从词表来。缺 `lang/` 的后果是全部显示成 key id
        // （`shop.form.button.buy` 之类），所以这里必须是 warn，不能是 debug。
        //
        // 这条与下面那条「载入失败」是 i18n 自己的启动日志：此刻词表还没载入，
        // 查表只会拿到 key id。它们靠 `kBootMessages` 兜底（见上），所以照样走词表通道。
        logger.warn(ui::msg("shop.log.text.no_lang_dir", dir.string()));
        return;
    }

    gLoaded = false;
    if (auto loaded = ll::i18n::getInstance().load(dir); !loaded) {
        logger.warn(ui::msg("shop.log.text.load_failed", dir.string(), loaded.error().message()));
        return;
    }
    gLoaded = true;
    // 这一条可以查表：载入成功意味着词表现在可用。
    logger.debug(ui::msg("shop.log.text.loaded", dir.string()));
}

std::string renderText(ss::shop::Text const& text, std::string_view localeCode) {
    // `sequence(...)`：本身没有文本，只是把几段接起来。
    if (text.key.empty() && text.literal.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < text.args.size(); ++i) {
            if (text.multiline && i != 0) {
                joined += '\n';
            }
            joined += renderText(text.args[i], localeCode);
        }
        return joined;
    }

    auto const    effectiveLocale = localeCode.empty() ? ll::i18n::getDefaultLocaleCode() : localeCode;
    std::string body = text.key.empty()
                         ? text.literal
                         : std::string{[&] {
                               auto const pattern = lookupOrBoot(text.key, effectiveLocale);
                               return pattern.empty() ? std::string_view{text.key} : pattern;
                           }()};
    if (!text.args.empty()) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for (auto const& arg : text.args) {
            store.push_back(renderText(arg, localeCode));
        }
        try {
            body = fmt::vformat(body, store);
        } catch (fmt::format_error const&) {
            // 词表里模板写坏了（`{}` 数量对不上）：原样输出，至少能看出是哪条。
        }
    }
    return std::string{ss::shop::text::colorOf(text.tone)} + body;
}

std::string translate(std::string_view chinese, std::string_view localeCode) {
    if (!gLoaded) {
        return std::string{chinese};
    }
    // key 就是中文原文：没有条目时 `get` 返回空视图，退回原文而不是显示成一个 id。
    auto const translated = ll::i18n::getInstance().get(chinese, localeCode);
    return translated.empty() ? std::string{chinese} : std::string{translated};
}

std::string plain(ss::shop::Text const& text) {
    return ss::shop::stripColorCodes(renderText(text, {}));
}

} // namespace ss::shop::ui
