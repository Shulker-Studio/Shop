#include "util/log/JsonLine.h"

namespace ss::shop {

nlohmann::ordered_json jsonShop(int dimensionId, int x, int y, int z) {
    nlohmann::ordered_json out;
    out["dim"] = dimensionId;
    out["x"]   = x;
    out["y"]   = y;
    out["z"]   = z;
    return out;
}

nlohmann::ordered_json jsonParty(std::string const& xuid, std::string const& name) {
    nlohmann::ordered_json out;
    out["xuid"] = xuid;
    out["name"] = name;
    return out;
}

nlohmann::ordered_json jsonItem(std::string const& id, int aux, std::string const& name) {
    nlohmann::ordered_json out;
    out["id"]   = id;
    out["aux"]  = aux;
    out["name"] = name;
    return out;
}

std::string jsonLine(nlohmann::ordered_json const& row) {
    // `ensure_ascii = false`：中文原样写（`\u94bb` 在文件里就找不到「钻石」了）。
    return row.dump(-1, ' ', /*ensure_ascii=*/false, nlohmann::ordered_json::error_handler_t::replace) + '\n';
}

} // namespace ss::shop
