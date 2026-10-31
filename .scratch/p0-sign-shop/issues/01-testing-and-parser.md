# 01: 测试设施与解析器

**What to build:** 引入 doctest 与 `xmake test`；把告示牌正面的四行文本解析成商店定义（份量、价格对、是否管理员店），并把规范化后的四行拼回去作为字符串。

这一层不碰引擎：不 include 任何 `mc/*` 头，全部是纯字符串进、结构体出。它是后面每一步的回归保护，也是唯一能在没有 BDS 的情况下自动验证的东西。

**Blocked by:** None（可立即开始）
**Status:** resolved

- [x] `xmake test` 能跑，至少一个测试用例通过
- [x] 价格行：只写 `B`、只写 `S`、两者都写、`free`、带 `K`/`M` 后缀、带小数、零、负数、缺冒号、多余冒号，各自的解析结果正确
- [x] 价格行：B 价低于 S 价时按配置拒绝
- [x] 份量行：合法整数通过；0、负数、超过上限、非数字、带空格、Java 的 `Q 64 : C 64` 计数格式全部拒绝
- [x] 份数计价：份价 × 份数、部分成交后的实际总价、1 份与上限份数的边界
- [x] 管理员商店判定：配置名 + 是 OP → 管理员店；配置名 + 非 OP → 普通店
- [x] 解析器与计价模块的源文件不含任何 `mc/` 头（用构建脚本或测试显式断言）

## Answer

**测试设施**：`xmake.lua` 加 `add_requires("doctest 2.5.3")` 与 `target("ShopTests")`（`set_default(false)` + `add_tests("default")`），只编译 `src/shop/**.cpp` 与 `tests/**.cpp`，不链接 LeviLamina；windows 上跟 `Shop` 一样用 clang-cl 与 `/utf-8`。`xmake test` 现在是 `31 test cases / 148 assertions` 全绿；普通 `xmake` 构建 `Shop.dll` 时也会跑同一条 `mc/` 头断言（共享 `check_pure_logic_headers()`）。

**纯逻辑层**：
- `src/shop/ShopSign.{h,cpp}`：`parseAmountLine`、`parsePriceLine`、`isAdminShop`、`parseShopSign`、`formatShopSign`、`joinSignText`、`stripColorCodes`。`joinSignText` 的分隔符是 `std::string_view`：默认真换行，H1 里要传字面 `"\n"`（编辑包的行分隔是两字符）。
- `src/shop/TradePricing.{h,cpp}`：`totalPrice`、`affordableUnits`、`tradeUnits`。
- 两者只 include 标准库；`ShopTests` 的 `before_build` 扫描 `src/shop/**.h|**.cpp`，出现 `#include <mc/` 或 `"mc/` 直接 `raise`（实测会拦住构建）。

**解析器实现的选择**（写进 DESIGN.md 5.2）：
- 小数向下取整：`B 1.5` → 1，`B 1.5K` → 1500；小数位最多 9 位，更多按非法。
- `free` 与 `0` 等价；规范化输出数值形式（`B 0`），不写 `free`，保证格式化结果可再次解析。
- 两段价格必须都带显式方向且不能重复；单段无方向按 B 处理；多余/空冒号、负数、`B10`（缺空格）、超出 `int64`（包括“整数部分没越界、加上小数后越界”）一律非法。
- `K`/`M` 与 `B`/`S` 都是大小写不敏感（对齐 Java 版先 upper 再解析），规范化输出大写。
- 第 1、2、3 行都先去颜色码（`parseAmountLine`/`parsePriceLine` 内部处理，`§a64` 能解析）；第 2 行仍不允许任何空白。
- 第 3 行去颜色码后不含 `B`/`S` → `NotAShopSign`，调用方必须原样放行（`SOLD OUT` 这类会停在 `BadPrice`，但 `looksLikeShopSign()` 为真，照样只提示、不删牌）。
- `B 价 < S 价` 由 `blockSellPriceHigherThanBuy` 控制，相等合法。
- 管理员店 = 第 1 行（去颜色码、去空白）等于 `adminShopName` **且**编辑者是 OP。

**测试用例组织**：`tests/main.cpp`（doctest main）、`tests/shop/ShopSignTest.cpp`、`tests/shop/TradePricingTest.cpp`。
