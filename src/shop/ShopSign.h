#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ss::shop {

/// 价格行解析出的价格对：买入价与卖出价，各自是「一份」的总价（恒为整数）。
/// B = 玩家买入（牌面记号，不是术语），S = 玩家卖出。
struct PricePair {
    std::optional<std::int64_t> buy;
    std::optional<std::int64_t> sell;
};

/// 参与解析的配置项，与 config.json 的字段一一对应。
struct ShopSignConfig {
    int         maxShopAmount               = 3456;
    std::string adminShopName               = "Admin Shop";
    bool        blockSellPriceHigherThanBuy = true;
};

/// 商店定义（解析结果）。
struct ShopSpec {
    int                         amount = 0; ///< 一份的件数
    std::optional<std::int64_t> buyPrice;   ///< 一份的买入价，玩家付给店主
    std::optional<std::int64_t> sellPrice;  ///< 一份的卖出价，店主付给玩家
    bool                        adminShop = false;
};

enum class ShopSignError {
    None,
    NotAShopSign,      ///< 第 3 行不含 B/S：不是商店牌，调用方必须原样放行
    EmptyOwnerName,    ///< 第 1 行去颜色码后为空
    BadAmount,         ///< 第 2 行不是 1..maxShopAmount 的整数
    BadPrice,          ///< 第 3 行不满足价格行文法
    SellPriceAboveBuy, ///< B 价低于 S 价，且配置要求拒绝
};

struct ShopSignResult {
    std::optional<ShopSpec> spec;
    ShopSignError           error = ShopSignError::None;

    [[nodiscard]] bool ok() const { return spec.has_value(); }

    /// 这块牌子是否「像商店牌」。不是商店牌的牌子不能被本模组改写。
    [[nodiscard]] bool looksLikeShopSign() const { return ok() || error != ShopSignError::NotAShopSign; }
};

/// 去掉 `§` 与其后的格式码（UTF-8 下的 0xC2 0xA7）。
std::string stripColorCodes(std::string_view text);

/// 解析第 2 行（份）。只接受 ASCII 十进制整数，且落在 `1 .. maxAmount`。
std::optional<int> parseAmountLine(std::string_view line, int maxAmount);

/// 解析第 3 行（价格）。
///
/// 文法：`segment (":" segment)?`，`segment := [B|S] amount | amount [B|S]`，
/// `amount := number | "free"`，`number := digits [ "." digits ] [ "K" | "M" ]`。
/// - 只有一段时按 B 处理；
/// - 两段都必须显式写明方向，且方向不能重复；
/// - `free` 与 `0` 等价；小数向下取整，小数位最多 9 位；
/// - 负数、空段、多余冒号、无法识别的记号、溢出都返回 `std::nullopt`。
std::optional<PricePair> parsePriceLine(std::string_view line);

/// 管理员商店判定：第 1 行等于配置名，且编辑者是 OP。
bool isAdminShop(std::string_view ownerLine, std::string_view adminShopName, bool editorIsOp);

/// 解析告示牌正面的四行文本。第 1、4 行只用于判定，不解析内容。
ShopSignResult
parseShopSign(const std::array<std::string_view, 4>& lines, bool editorIsOp, const ShopSignConfig& config);

/// 规范化：把商店定义拼回四行文本。第 1 行的名字由调用方决定
/// （普通店传店主显示名，管理员店传配置名）。
std::array<std::string, 4>
formatShopSign(const ShopSpec& spec, std::string_view ownerDisplayName, std::string_view itemDisplayName);

/// 把四行拼成一个字符串。分隔符默认是真正的换行符；告示牌编辑包（H1）里
/// `PlayerEditSignBeforeEvent::text()` 用的是字面两字符 `\n`，调用方传 `"\\n"`。
std::string joinSignText(const std::array<std::string, 4>& lines, std::string_view separator = "\n");

} // namespace ss::shop
