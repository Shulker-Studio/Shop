#include "shop/ShopSign.h"

#include "shop/Text.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ss::shop {
namespace {

/// 小数位超过这个长度就按非法处理：金额精度是整数，不允许用它刷精度。
constexpr std::size_t kMaxFractionDigits = 9;

/// 整数部分超过这个位数必然溢出 `int64`，先挡掉再累加。
constexpr std::size_t kMaxIntegerDigits = 19;

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::vector<std::string_view> splitWhitespace(std::string_view text) {
    std::vector<std::string_view> tokens;
    std::size_t                   i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
        }
        const std::size_t begin = i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) {
            ++i;
        }
        if (begin != i) {
            tokens.push_back(text.substr(begin, i - begin));
        }
    }
    return tokens;
}

std::vector<std::string_view> splitOn(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t                   begin = 0;
    while (true) {
        const std::size_t pos = text.find(separator, begin);
        if (pos == std::string_view::npos) {
            parts.push_back(text.substr(begin));
            break;
        }
        parts.push_back(text.substr(begin, pos - begin));
        begin = pos + 1;
    }
    return parts;
}

std::string toLower(std::string_view text) {
    std::string lower;
    lower.reserve(text.size());
    for (const char c : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower;
}

/// 价格行里的方向（B = 买入，S = 卖出）。大小写不敏感，对齐 Java 版先 upper 再解析的行为。
enum class PriceDirection { Buy, Sell };

std::optional<PriceDirection> priceDirectionOf(char c) {
    if (c == 'b' || c == 'B') {
        return PriceDirection::Buy;
    }
    if (c == 's' || c == 'S') {
        return PriceDirection::Sell;
    }
    return std::nullopt;
}

/// 方向记号是一个单独成词的 `B` 或 `S`。
std::optional<PriceDirection> priceDirectionOf(std::string_view token) {
    if (token.size() != 1) {
        return std::nullopt;
    }
    return priceDirectionOf(token[0]);
}

/// 逐位解析十进制数字，超过 `limit` 或超过位数上限即失败。
std::optional<std::uint64_t> parseDigits(std::string_view digits, std::uint64_t limit) {
    if (digits.empty() || digits.size() > kMaxIntegerDigits) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : digits) {
        if (!isDigit(c)) {
            return std::nullopt;
        }
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
        if (value > limit) {
            return std::nullopt;
        }
    }
    return value;
}

/// 解析一个金额记号：`free`、整数、小数、以及可选的 K/M 后缀（小数向下取整）。
std::optional<std::int64_t> parseAmountValue(std::string_view token) {
    const std::string lower = toLower(token);
    std::string_view  text{lower};
    if (text.empty()) {
        return std::nullopt;
    }
    if (text == "free") {
        return std::int64_t{0};
    }

    std::uint64_t multiplier = 1;
    if (text.back() == 'k') {
        multiplier = 1000;
        text.remove_suffix(1);
    } else if (text.back() == 'm') {
        multiplier = 1000000;
        text.remove_suffix(1);
    }
    if (text.empty()) {
        return std::nullopt;
    }

    const std::size_t dot = text.find('.');
    if (dot != std::string_view::npos && text.find('.', dot + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view integerPart  = dot == std::string_view::npos ? text : text.substr(0, dot);
    const std::string_view fractionPart = dot == std::string_view::npos ? std::string_view{} : text.substr(dot + 1);
    if (integerPart.empty() || (dot != std::string_view::npos && fractionPart.empty())) {
        return std::nullopt;
    }
    if (fractionPart.size() > kMaxFractionDigits) {
        return std::nullopt;
    }

    auto       maxValue = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto integer  = parseDigits(integerPart, maxValue / multiplier);
    if (!integer.has_value()) {
        return std::nullopt;
    }

    std::uint64_t fraction    = 0;
    std::uint64_t fractionDiv = 1;
    if (!fractionPart.empty()) {
        const auto parsed = parseDigits(fractionPart, std::numeric_limits<std::uint64_t>::max());
        if (!parsed.has_value()) {
            return std::nullopt;
        }
        fraction = *parsed;
        for (std::size_t i = 0; i < fractionPart.size(); ++i) {
            fractionDiv *= 10;
        }
    }

    /// 小数部分对总价的贡献向下取整；`fraction < 10^9`、`multiplier <= 10^6`，不会溢出。
    const std::uint64_t contribution = fraction * multiplier / fractionDiv;
    const std::uint64_t base         = *integer * multiplier;
    // 整数部分已按 maxValue / multiplier 限幅，但加上小数贡献后仍可能越界
    if (base > maxValue - contribution) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(base + contribution);
}

/// 第 3 行去颜色码后是否含 `B` 或 `S`。
bool containsPriceMarker(std::string_view text) {
    return std::any_of(text.begin(), text.end(), [](char c) { return priceDirectionOf(c).has_value(); });
}

} // namespace

std::string stripColorCodes(std::string_view text) {
    std::string plain;
    plain.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        // UTF-8 的 `§` 是 0xC2 0xA7，后面最多跟一个格式码字符
        if (static_cast<unsigned char>(text[i]) == 0xC2 && i + 1 < text.size()
            && static_cast<unsigned char>(text[i + 1]) == 0xA7) {
            i += 2;
            if (i < text.size()) {
                ++i;
            }
            continue;
        }
        plain.push_back(text[i]);
        ++i;
    }
    return plain;
}

std::optional<int> parseAmountLine(std::string_view line, int maxAmount) {
    // 与价格行对齐：先去掉颜色码；空白仍然一律拒绝（牌面第 2 行是权威整数）
    const std::string plain = stripColorCodes(line);
    if (plain.empty() || maxAmount < 1) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    for (const char c : plain) {
        if (!isDigit(c)) {
            return std::nullopt;
        }
        value = value * 10 + static_cast<std::int64_t>(c - '0');
        if (value > maxAmount) {
            return std::nullopt;
        }
    }
    if (value < 1) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

std::optional<PricePair> parsePriceLine(std::string_view line) {
    const std::string      plain = stripColorCodes(line);
    const std::string_view text  = trimView(plain);
    if (text.empty()) {
        return std::nullopt;
    }

    const std::vector<std::string_view> segments = splitOn(text, ':');
    if (segments.size() > 2) {
        return std::nullopt;
    }
    const bool single = segments.size() == 1;

    PricePair prices;
    bool      seenBuy  = false;
    bool      seenSell = false;

    for (const std::string_view rawSegment : segments) {
        const std::string_view segment = trimView(rawSegment);
        if (segment.empty()) {
            return std::nullopt;
        }
        const std::vector<std::string_view> tokens = splitWhitespace(segment);

        std::string_view amountToken;
        bool             buy = true;
        if (single && tokens.size() == 1) {
            // 只有一段且没写方向：按 B 处理
            amountToken = tokens.front();
        } else {
            if (tokens.size() != 2) {
                return std::nullopt;
            }
            const std::optional<PriceDirection> firstDirection  = priceDirectionOf(tokens[0]);
            const std::optional<PriceDirection> secondDirection = priceDirectionOf(tokens[1]);
            if (firstDirection.has_value() == secondDirection.has_value()) {
                // 两个都是方向标记（缺金额），或都不是（多段时方向必须显式）
                return std::nullopt;
            }
            if (firstDirection.has_value()) {
                buy         = *firstDirection == PriceDirection::Buy;
                amountToken = tokens[1];
            } else {
                buy         = *secondDirection == PriceDirection::Buy;
                amountToken = tokens[0];
            }
        }

        const auto value = parseAmountValue(amountToken);
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (buy) {
            if (seenBuy) {
                return std::nullopt;
            }
            seenBuy    = true;
            prices.buy = value;
        } else {
            if (seenSell) {
                return std::nullopt;
            }
            seenSell    = true;
            prices.sell = value;
        }
    }

    if (!prices.buy.has_value() && !prices.sell.has_value()) {
        return std::nullopt;
    }
    return prices;
}

bool sellPriceHigherThanBuy(
    std::optional<std::int64_t> buy,
    std::optional<std::int64_t> sell,
    ShopSignConfig const&       config
) {
    if (!config.blockSellPriceHigherThanBuy) {
        return false;
    }
    return buy.has_value() && sell.has_value() && *buy < *sell;
}

std::string sellPriceAboveBuyMessage(std::optional<std::int64_t> buy, std::optional<std::int64_t> sell) {
    if (!buy || !sell) {
        // 手写编辑报错时手上没有「改成了什么」：牌面就在眼前，不必把号码再念一遍。
        return "卖出价不能高于买入价";
    }
    return "卖出价 " + std::to_string(*sell) + " 高于买入价 " + std::to_string(*buy) + "：卖出价不能高于买入价";
}

bool isAdminShop(std::string_view ownerLine, std::string_view adminShopName, bool editorIsOp) {
    if (!editorIsOp) {
        return false;
    }
    return trimCopy(stripColorCodes(ownerLine)) == adminShopName;
}

ShopSignResult
parseShopSign(const std::array<std::string_view, 4>& lines, bool editorIsOp, const ShopSignConfig& config) {
    // 第 1、2、3 行都先去掉颜色码；第 2 行仍然不允许空白（ticket 要求「带空格」一律拒绝）
    const std::string pricePlain = stripColorCodes(lines[2]);
    if (!containsPriceMarker(pricePlain)) {
        return {std::nullopt, ShopSignError::NotAShopSign};
    }

    const std::string ownerName = trimCopy(stripColorCodes(lines[0]));
    if (ownerName.empty()) {
        return {std::nullopt, ShopSignError::EmptyOwnerName};
    }

    const auto amount = parseAmountLine(lines[1], config.maxShopAmount);
    if (!amount.has_value()) {
        return {std::nullopt, ShopSignError::BadAmount};
    }

    const auto prices = parsePriceLine(lines[2]);
    if (!prices.has_value()) {
        return {std::nullopt, ShopSignError::BadPrice};
    }
    if (sellPriceHigherThanBuy(prices->buy, prices->sell, config)) {
        return {std::nullopt, ShopSignError::SellPriceAboveBuy};
    }

    ShopSpec spec;
    spec.amount    = *amount;
    spec.buyPrice  = prices->buy;
    spec.sellPrice = prices->sell;
    spec.adminShop = isAdminShop(ownerName, config.adminShopName, editorIsOp);
    return {spec, ShopSignError::None};
}

std::array<std::string, 4>
formatShopSign(const ShopSpec& spec, std::string_view ownerDisplayName, std::string_view itemDisplayName) {
    std::array<std::string, 4> lines;
    lines[0] = std::string{ownerDisplayName};
    lines[1] = std::to_string(spec.amount);

    std::string priceLine;
    if (spec.buyPrice.has_value()) {
        priceLine = "B " + std::to_string(*spec.buyPrice);
    }
    if (spec.sellPrice.has_value()) {
        if (!priceLine.empty()) {
            priceLine += " : ";
        }
        priceLine += "S " + std::to_string(*spec.sellPrice);
    }
    lines[2] = std::move(priceLine);
    lines[3] = std::string{itemDisplayName};
    return lines;
}

ShopSignResult parseShopSignText(std::string_view text, bool editorIsOp, const ShopSignConfig& config) {
    auto const                            split = splitSignText(text);
    std::array<std::string_view, 4> const lines{split[0], split[1], split[2], split[3]};
    return parseShopSign(lines, editorIsOp, config);
}

std::string joinSignText(const std::array<std::string, 4>& lines, std::string_view separator) {
    std::string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            text += separator;
        }
        text += lines[i];
    }
    return text;
}

std::array<std::string, 4> splitSignText(std::string_view text, std::string_view separator) {
    std::array<std::string, 4> lines{};
    if (separator.empty()) {
        lines[0] = std::string{text};
        return lines;
    }

    std::size_t line = 0;
    std::size_t from = 0;
    while (line < lines.size()) {
        std::size_t const hit = text.find(separator, from);
        if (hit == std::string_view::npos) {
            lines[line] = std::string{text.substr(from)};
            break;
        }
        lines[line] = std::string{text.substr(from, hit - from)};
        ++line;
        from = hit + separator.size();
    }
    return lines;
}

} // namespace ss::shop
