#pragma once

#include "shop/ShopSign.h" // stripColorCodes
#include "shop/Text.h"

#include <string>

/// 断言用的「玩家看到的文本」： 渲染（走测试注入的 renderer）+ 去掉颜色码。
///
/// 颜色是外观、不是契约，所以测试只比文字；渲染本身由 `tests/main.cpp` 的 renderer 负责。
inline std::string plainText(ss::shop::Text const& text) {
    return ss::shop::stripColorCodes(ss::shop::text::render(text));
}
