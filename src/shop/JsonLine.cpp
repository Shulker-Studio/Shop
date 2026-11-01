#include "shop/JsonLine.h"

namespace ss::shop {

std::string jsonLine(nlohmann::ordered_json const& row) {
    // `ensure_ascii = false`：中文原样写（`\u94bb` 在文件里就找不到「钻石」了）。
    return row.dump(-1, ' ', /*ensure_ascii=*/false, nlohmann::ordered_json::error_handler_t::replace) + '\n';
}

} // namespace ss::shop
