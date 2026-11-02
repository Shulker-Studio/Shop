#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "shop/Text.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>
#include <string_view>

namespace {

nlohmann::json gTexts;

/// 测试用的是**真实的 `lang/zh_CN.json`**： 断言里的中文就是玩家看到的中文，
/// 于是「词表少了一条 key」会表现为测试失败，而不是上线后显示成 `shop.form.xxx`。
std::string_view lookup(std::string_view key) {
    auto const it = gTexts.find(std::string{key});
    if (it == gTexts.end() || !it->is_string()) {
        return {};
    }
    return it->get_ref<nlohmann::json::string_t const&>();
}

/// 测试用的渲染器： 查 `lang/zh_CN.json` 拿模板，再按顺序填 `{}`。
///
/// 生产环境用的是 `ll::i18n` + `fmt`（`src/mod/Text.cpp`）；这里手写一份是为了让
/// `ShopTests` 不依赖引擎——**只服务断言**，不进 `src/`。
std::string renderText(ss::shop::Text const& text, std::string_view) {
    if (text.key.empty() && text.literal.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < text.args.size(); ++i) {
            if (text.multiline && i != 0) {
                joined += '\n';
            }
            joined += renderText(text.args[i], {});
        }
        return joined;
    }

    if (text.key.empty()) {
        return std::string{ss::shop::text::colorOf(text.tone)} + text.literal;
    }

    // 词表缺 key、或 `{}` 数量与参数个数对不上，都渲染成显眼的标记： 
    // 断言里出现 `<missing:...>` / `<bad-args:...>` 就是测试失败，而不是上线后才发现。
    auto const  pattern = lookup(text.key);
    std::string body    = pattern.empty() ? ("<missing:" + text.key + ">") : std::string{pattern};
    std::size_t slots   = 0;
    for (std::size_t i = 0; i + 1 < body.size(); ++i) {
        if (body[i] == '{' && body[i + 1] == '}') {
            ++slots;
            ++i;
        }
    }
    if (slots != text.args.size()) {
        body = "<bad-args:" + text.key + ">";
    }
    if (!text.args.empty()) {
        std::string filled;
        std::size_t arg = 0;
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '{' && i + 1 < body.size() && body[i + 1] == '}') {
                if (arg < text.args.size()) {
                    filled += renderText(text.args[arg++], {});
                }
                ++i;
                continue;
            }
            filled += body[i];
        }
        body = std::move(filled);
    }
    return std::string{ss::shop::text::colorOf(text.tone)} + body;
}

} // namespace

int main(int argc, char** argv) {
    std::ifstream in{"lang/zh_CN.json"};
    if (in) {
        in >> gTexts;
    }
    ss::shop::text::installLookup(lookup);
    ss::shop::text::installRenderer(renderText);

    doctest::Context context{argc, argv};
    return context.run();
}
