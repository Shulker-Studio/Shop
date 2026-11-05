#include "util/log/Transaction.h"

#include "util/log/Files.h"
#include "Shop.h"
#include "util/log/Rotation.h"

#include <string>
#include <vector>
#include "util/i18n/TextRender.h"

namespace ss::shop {

void writeTradeRecord(TradeRecord const& record) {
    if (auto const problem = tradeRecordProblem(record); !problem.empty()) {
        Shop::getInstance().getSelf().getLogger().error(ui::msg(
                "shop.log.trade.record_inconsistent",
                ui::plain(text::keyed(std::string{problem})),
                record.shop.x,
                record.shop.y,
                record.shop.z
            )
        );
        return;
    }

    // 文件名由 `ts` 推出来（`transactions-YYYY-MM-DD.jsonl`）：跨天自动换文件，
    // 所以不缓存「当前文件」。
    appendLogLines(transactionLogDirectory(), transactionLogFileName(isoDateOf(record.ts)), {tradeRecordJson(record)});
}

} // namespace ss::shop
