#include "shop/TradePricing.h"

#include <doctest/doctest.h>
#include <cmath>
#include <limits>
#include <optional>

namespace {
using namespace ss::shop;
}

TEST_CASE("总价 = 份价 × 份数") {
    CHECK(totalPrice(10, 4) == std::optional<std::int64_t>{40});
    CHECK(totalPrice(10, 1) == std::optional<std::int64_t>{10});
    CHECK(totalPrice(0, 3456) == std::optional<std::int64_t>{0});
    CHECK(totalPrice(10, 0) == std::optional<std::int64_t>{0});
}

TEST_CASE("总价: 负数与溢出被拒绝") {
    CHECK_FALSE(totalPrice(10, -1).has_value());
    CHECK_FALSE(totalPrice(-10, 1).has_value());
    CHECK_FALSE(totalPrice(std::numeric_limits<std::int64_t>::max(), 2).has_value());
}

TEST_CASE("份数上限: 资金可承受量") {
    CHECK(affordableUnits(10, 95) == 9);
    CHECK(affordableUnits(10, 100) == 10);
    CHECK(affordableUnits(10, 5) == 0);
    CHECK(affordableUnits(10, -5) == 0);
    CHECK(affordableUnits(0, 0) == std::numeric_limits<int>::max());
}

TEST_CASE("部分成交: 取请求量与各上限的最小值") {
    CHECK(tradeUnits(4, 10, 10, 10) == 4);
    CHECK(tradeUnits(4, 2, 10, 10) == 2);
    CHECK(tradeUnits(4, 10, 10, 3) == 3);
    CHECK(tradeUnits(4, 10, 1, 10) == 1);
    CHECK(tradeUnits(4, 0, 10, 10) == 0);
    CHECK(tradeUnits(0, 10, 10, 10) == 0);
}

TEST_CASE("部分成交: 上限份数的边界") {
    CHECK(tradeUnits(3456, 3456, 3456, 3456) == 3456);
    CHECK(tradeUnits(3457, 3456, 3456, 3456) == 3456);
    CHECK(totalPrice(11, tradeUnits(3456, 3456, 3456, 3456)) == std::optional<std::int64_t>{11 * 3456});
}

TEST_CASE("部分成交后的实际总价按实际份数重算") {
    auto units = tradeUnits(4, 2, 10, 10);
    CHECK(totalPrice(10, units) == std::optional<std::int64_t>{20});
}

TEST_CASE("件数 → 份数: 向下取整") {
    CHECK(unitsFromItems(64, 64) == 1);
    CHECK(unitsFromItems(127, 64) == 1);
    CHECK(unitsFromItems(128, 64) == 2);
    CHECK(unitsFromItems(3456, 64) == 54);
    CHECK(unitsFromItems(0, 64) == 0);
}

TEST_CASE("件数 → 份数: 每份件数非法时视为 0 份") {
    CHECK(unitsFromItems(64, 0) == 0);
    CHECK(unitsFromItems(64, -1) == 0);
    CHECK(unitsFromItems(-64, 64) == 0);
}

TEST_CASE("商店税: 总额 × 税率，向下取整到整数") {
    CHECK(shopTax(100, 0.05) == 5);
    CHECK(shopTax(100, 0.0) == 0);
    // 向下取整： 3.5 → 3，余数留给店主（DESIGN §8.9）
    CHECK(shopTax(70, 0.05) == 3);
    CHECK(shopTax(99, 0.05) == 4);
    // 不足 1 的税按 1 收（下限 1，见下一个用例）
    CHECK(shopTax(1, 0.05) == 1);
    CHECK(shopTax(19, 0.05) == 1);
}

TEST_CASE("商店税: 税额下限 1，拆单不能把税拆没") {
    // 100 元 5% 应缴 5；若拆成 20 笔 5 元，每笔 0.25 → 每笔按 1 收，共 20，拆单反而更贵。
    CHECK(shopTax(100, 0.05) == 5);
    for (int i = 0; i < 20; ++i) {
        CHECK(shopTax(5, 0.05) == 1);
    }
    // 下限只对有税的交易成立： 没有税 / 总额非正仍然是 0，不能凭空收 1。
    CHECK(shopTax(5, 0.0) == 0);
    CHECK(shopTax(0, 0.05) == 0);
    CHECK(shopTax(-5, 0.05) == 0);
}

TEST_CASE("税率精度: 只接受千分位网格上的值") {
    CHECK(isTaxRateOnGrid(0.0));
    CHECK(isTaxRateOnGrid(0.001));
    CHECK(isTaxRateOnGrid(0.05));
    CHECK(isTaxRateOnGrid(0.29));   // 浮点下 289.99999999999994，容差必须留给它
    CHECK(isTaxRateOnGrid(0.999));
    CHECK_FALSE(isTaxRateOnGrid(0.0005));
    CHECK_FALSE(isTaxRateOnGrid(0.0505));
    // 越界一律不算「网格上」（范围由 validate 另外报错）
    CHECK_FALSE(isTaxRateOnGrid(-0.001));
    CHECK_FALSE(isTaxRateOnGrid(1.0));
    CHECK_FALSE(isTaxRateOnGrid(std::nan("")));
}

TEST_CASE("商店税: 非法输入不产生负税或超额的税") {
    CHECK(shopTax(0, 0.5) == 0);
    CHECK(shopTax(-100, 0.5) == 0);   // 总额非正
    CHECK(shopTax(100, -0.1) == 0);   // 负税率
    CHECK(shopTax(100, 0.0) == 0);
    CHECK(shopTax(100, std::nan("")) == 0); // NaN 税率当没有税，不能算出个随机数
    CHECK(shopTax(100, 1.0) == 100);  // 全额征税封顶到总额
    CHECK(shopTax(100, 2.0) == 100);  // 税率 > 1 也不超额
    // 极大总额不溢出、也不越过总额
    CHECK(shopTax(std::numeric_limits<std::int64_t>::max(), 0.05) > 0);
    CHECK(shopTax(std::numeric_limits<std::int64_t>::max(), 0.05)
          < std::numeric_limits<std::int64_t>::max());
}

TEST_CASE("含税份数上限: 卖出方向店主多付一份税") {
    // 单价 10、税率 10%： 一份含税成本 11；100 块钱够 9 份（99），不够 10 份（110）
    CHECK(affordableUnitsWithTax(10, 100, 0.1) == 9);
    CHECK(affordableUnitsWithTax(10, 110, 0.1) == 10);
    // 与不含税口径一致： 没有税时不多不少
    CHECK(affordableUnitsWithTax(10, 100, 0.0) == affordableUnits(10, 100));
    // 免费店： 钱不是限制
    CHECK(affordableUnitsWithTax(0, 0, 0.1) == std::numeric_limits<int>::max());
    // NaN 税率当作没有税
    CHECK(affordableUnitsWithTax(10, 100, std::nan("")) == affordableUnits(10, 100));
    // 没钱就是 0 份
    CHECK(affordableUnitsWithTax(10, 0, 0.1) == 0);
    // 保守： 算出来的份数一定付得起（含税成本 = 总额 + 税）
    for (int units : {1, 2, 3, 5, 10}) {
        std::int64_t const total = 10 * units;
        std::int64_t const cost  = total + shopTax(total, 0.1);
        CHECK(affordableUnitsWithTax(10, cost, 0.1) >= units);
        CHECK(affordableUnitsWithTax(10, cost - 1, 0.1) == (units > 1 ? units - 1 : 0));
    }
}

TEST_CASE("无限库存: tradeUnits 的 min 里没有「库存」这一项 (DESIGN §10.1 / §10.3)") {
    auto const unlimited = StockView::makeUnlimited();

    // 请求量才是上限： 无上限的库存项不参与 min，也不靠一个很大的数糊过去
    CHECK(tradeUnits(4, unlimited, 10, 10) == 4);
    // 「收得下」也可以是无限（卖出方向的商店箱，票 02）
    CHECK(tradeUnits(4, 10, unlimited, 10) == 4);
    CHECK(tradeUnits(4, unlimited, unlimited, 4) == 4);
    // 其余上限照旧生效： 付得起的份数
    CHECK(tradeUnits(4, unlimited, 10, 3) == 3);
    // 有限的那一项仍然压上限（无限只关掉它自己那一项）
    CHECK(tradeUnits(4, unlimited, 2, 10) == 2);
    CHECK(tradeUnits(4, StockView::makeFinite(2), unlimited, 10) == 2);
    // 有限 0 份是真的空了，不许被当成「无限」
    CHECK(tradeUnits(4, StockView::makeFinite(0), unlimited, 10) == 0);
    // 大数照旧，不会被哨兵截成别的意思
    CHECK(tradeUnits(3456, unlimited, unlimited, 3456) == 3456);
}
