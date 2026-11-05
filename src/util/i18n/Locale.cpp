#include "util/i18n/Locale.h"

#include "util/i18n/Message.h"
#include "util/i18n/Text.h"
#include "core/sign/ShopSign.h"
#include "Shop.h"

#include "fmt/format.h"
#include "ll/api/i18n/I18n.h"
#include "mc/world/actor/player/Player.h"

#include <filesystem>

namespace ss::shop::ui {
namespace {

/// 语言文件是否真的载入成功。
///
/// 没载入时 `I18n::get` 对所有键都返回空，那与「没有这条翻译」无法区分，所以先在这里
/// 记一笔，省掉每条消息都去查一次空表。
bool gLoaded = false;

} // namespace

void loadTexts() {
    // 纯逻辑层的表单文本走同一张表：把 `ll::i18n` 的查表注入进去。
    // 就算没有语言文件也注入——查不到条目时 `get` 返回空，`text::tr` 会退回中文原文。
    text::installLookup([](std::string_view key) -> std::string_view {
        return ll::i18n::getInstance().get(key, {});
    });
    text::installRenderer(&renderText);

    auto&                       logger = Shop::getInstance().getSelf().getLogger();
    std::filesystem::path const dir    = Shop::getInstance().getSelf().getLangDir();

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        // **这不是可选项**：结构化文本改造之后，代码里已经没有中文原文了，
        // 玩家看到的每一句话都从词表来。缺 `lang/` 的后果是全部显示成 key id
        // （`shop.form.button.buy` 之类），所以这里必须是 warn，不能是 debug。
        //
        // 这条与下面那条「载入失败」**故意不走 i18n**：它们就是这个 i18n 自己的启动日志，
        // 此刻词表还没载入，查表只会拿到 key id，比中文原文更难读。这是全项目**仅有的
        // 两处**硬编码中文运行时输出，属于不可避免的 bootstrap 例外，不要"顺手统一"掉。
        logger.warn("没有 lang/ 目录（{}）：玩家文本会显示成 key id，请重新安装本模组", dir.string());
        return;
    }

    gLoaded = false;
    if (auto loaded = ll::i18n::getInstance().load(dir); !loaded) {
        logger.warn(
            "载入 lang/ 失败（{}）：{}；玩家文本会显示成 key id，请重新安装本模组",
            dir.string(),
            loaded.error().message()
        );
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
                               auto const pattern = ll::i18n::getInstance().get(text.key, effectiveLocale);
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
