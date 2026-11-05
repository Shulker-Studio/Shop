#include "util/i18n/Text.h"

namespace ss::shop {
namespace text {
namespace {

Lookup   gLookup   = nullptr;
Renderer gRenderer = nullptr;

} // namespace

void installLookup(Lookup lookup) { gLookup = lookup; }
void installRenderer(Renderer renderer) { gRenderer = renderer; }

std::string tr(std::string_view key) {
    if (gLookup == nullptr) {
        return std::string{key};
    }
    auto const found = gLookup(key);
    return found.empty() ? std::string{key} : std::string{found};
}

std::string_view colorOf(Tone tone) {
    switch (tone) {
    case Tone::Label:
        return kLabelColor;
    case Tone::Value:
        return kValueColor;
    case Tone::Accent:
        return kAccentColor;
    case Tone::Good:
        return kGoodColor;
    case Tone::Bad:
        return kBadColor;
    case Tone::Warn:
        return kWarnColor;
    case Tone::Muted:
        return kMutedColor;
    case Tone::Title:
        return kTitleColor;
    case Tone::Default:
        break;
    }
    return {};
}

std::string render(Text const& text, std::string_view localeCode) {
    if (gRenderer != nullptr) {
        return gRenderer(text, localeCode);
    }
    // 没注入渲染器（模组没启用 / 测试没装）：只保证能读，不填 `{}`、不上色。
    if (text.key.empty() && text.literal.empty()) {
        std::string joined;
        for (std::size_t i = 0; i < text.args.size(); ++i) {
            if (text.multiline && i != 0) {
                joined += '\n';
            }
            joined += render(text.args[i], localeCode);
        }
        return joined;
    }
    return text.key.empty() ? text.literal : tr(text.key);
}

std::string renderLines(std::vector<Text> const& lines, std::string_view localeCode) {
    std::string content;
    for (auto const& line : lines) {
        if (!content.empty()) {
            content += '\n';
        }
        content += render(line, localeCode);
    }
    if (!content.empty()) {
        content += kReset;
    }
    return content;
}

Text lit(std::string value, Tone tone) { return Text{tone, {}, std::move(value), {}}; }

Text keyed(std::string key, Tone tone, TextArgs args) { return Text{tone, std::move(key), {}, std::move(args)}; }

Text field(Text label, Text value) {
    // 默认白，但调用方可以自己指定（价格金、余额绿、不可交易原因红）：
    // 强制 Value 会让「这一行要说的事」丢掉颜色。
    if (value.tone == Tone::Default) {
        value.tone = Tone::Value;
    }
    TextArgs args;
    args.reserve(2);
    args.push_back(std::move(label));
    args.push_back(std::move(value));
    return Text{Tone::Label, "shop.form.field", {}, std::move(args)};
}

Text joined(Text label, Text value) {
    if (value.tone == Tone::Default) {
        value.tone = Tone::Value;
    }
    TextArgs args;
    args.reserve(2);
    args.push_back(std::move(label));
    args.push_back(std::move(value));
    return Text{Tone::Default, "shop.form.joined", {}, std::move(args)};
}

Text sequence(TextArgs parts) { return Text{Tone::Default, {}, {}, std::move(parts), false}; }

Text block(std::vector<Text> lines) { return Text{Tone::Default, {}, {}, std::move(lines), true}; }

Text heading(Text body) {
    body.tone = Tone::Accent;
    return body;
}

Text muted(Text body) {
    body.tone = Tone::Muted;
    return body;
}

Text section(Text title) {
    TextArgs args;
    args.reserve(1);
    args.push_back(std::move(title));
    return Text{Tone::Muted, "shop.form.section", {}, std::move(args)};
}

} // namespace text
} // namespace ss::shop
