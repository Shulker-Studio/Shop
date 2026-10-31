#include "shop/TradePricing.h"

#include <doctest/doctest.h>
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

TEST_CASE("总价：负数与溢出被拒绝") {
    CHECK_FALSE(totalPrice(10, -1).has_value());
    CHECK_FALSE(totalPrice(-10, 1).has_value());
    CHECK_FALSE(totalPrice(std::numeric_limits<std::int64_t>::max(), 2).has_value());
}

TEST_CASE("份数上限：资金可承受量") {
    CHECK(affordableUnits(10, 95) == 9);
    CHECK(affordableUnits(10, 100) == 10);
    CHECK(affordableUnits(10, 5) == 0);
    CHECK(affordableUnits(10, -5) == 0);
    CHECK(affordableUnits(0, 0) == std::numeric_limits<int>::max());
}

TEST_CASE("部分成交：取请求量与各上限的最小值") {
    CHECK(tradeUnits(4, 10, 10, 10) == 4);
    CHECK(tradeUnits(4, 2, 10, 10) == 2);
    CHECK(tradeUnits(4, 10, 10, 3) == 3);
    CHECK(tradeUnits(4, 10, 1, 10) == 1);
    CHECK(tradeUnits(4, 0, 10, 10) == 0);
    CHECK(tradeUnits(0, 10, 10, 10) == 0);
}

TEST_CASE("部分成交：上限份数的边界") {
    CHECK(tradeUnits(3456, 3456, 3456, 3456) == 3456);
    CHECK(tradeUnits(3457, 3456, 3456, 3456) == 3456);
    CHECK(totalPrice(11, tradeUnits(3456, 3456, 3456, 3456)) == std::optional<std::int64_t>{11 * 3456});
}

TEST_CASE("部分成交后的实际总价按实际份数重算") {
    auto units = tradeUnits(4, 2, 10, 10);
    CHECK(totalPrice(10, units) == std::optional<std::int64_t>{20});
}
