#pragma once

#include "shop/TradeRecord.h"

namespace ss::shop {

/// 追加写一条成交流水到 `plugins/Shop/logs/transactions-YYYY-MM-DD.jsonl`（spec D1）。
///
/// 只写**终态**，而且「哪种终态值得记」由调用方（`finishTrade`）决定：
/// `NeedsConfirm` 不是终态，写进去就是噪音，还会被读成「失败」。
///
/// 落在**成交完成之后**：先动钱动货、再写流水。写失败只 `warn`（同进程一次），
/// 交易照常成功——流水不是交易的一部分，磁盘满了不该让全服商店瘫痪。
///
/// 矛盾记录（`tradeRecordProblem` 非空）**不写**，只 `error` 一句：一条 `result: "ok"`
/// 还带 `reason` 的行会让服主以为这笔生意失败过，比少一行更误导。
void writeTradeRecord(TradeRecord const& record);

} // namespace ss::shop
