#include "mod/TransactionLog.h"

#include "mod/LogFile.h"
#include "mod/Shop.h"

#include <string>
#include <vector>

namespace ss::shop {

void writeTradeRecord(TradeRecord const& record) {
    if (auto const problem = tradeRecordProblem(record); !problem.empty()) {
        Shop::getInstance().getSelf().getLogger().error(
            "交易流水记录自相矛盾（{}），这一行不写：({}, {}, {})",
            problem,
            record.shop.x,
            record.shop.y,
            record.shop.z
        );
        return;
    }

    // 文件名由 `ts` 推出来（`transactions-YYYY-MM-DD.jsonl`）：跨天自动换文件，
    // 所以不缓存「当前文件」。
    appendLogLines(logDirectory(), transactionLogFileName(record.ts.substr(0, 10)), {tradeRecordJson(record)});
}

} // namespace ss::shop
