# 02: 失败与冻结也留痕 + 被销毁物品留 NBT

**What to build:** 被拒的交易、以及补偿失败被冻结的交易，都各留一行（`fail` / `frozen`）；管理员店卖出的货被销毁时，往 `discarded-YYYY-MM-DD.jsonl` 写一行含完整 SNBT 的记录。

**Blocked by:** 01（`discarded` 的**真机验收**还要等 `p1-admin-unlimited-stock` 票 02 落地——在那之前普通店没有"销毁"这件事，只能用单测覆盖）

**Status:** ready-for-agent

- [ ] `Rejected → fail` + `reason`（由 `TradeRejection` 映射成机器可读短名，如 `no_money` / `no_stock` / `no_space` / `owner_offline` / `shop_closed`，**不把中文文案写进 reason**）
- [ ] `Broken → frozen` + `reason: compensation_failed`，且必须与那条 `fatal` 日志同时出现
- [ ] `NeedsConfirm` **不写**（不是终态，玩家可能直接关掉确认表单）
- [ ] 同一对「玩家 + 商店」被冻结后的后续提交记 `fail` + `reason: frozen_pair`（不重复写 `frozen`）
- [ ] 免费店（`gross = 0` / `total = 0`）仍记一行 `ok`（"0 元的成交"是有意义的历史）
- [ ] 管理员店：`admin: true`、`owner.name = adminShopName`、`owner.xuid = ""`、`tax = 0`、`backend` 如实写
- [ ] **`discarded` 文件**：管理员店卖出方向（货被销毁）额外写 N 行，一行一件物品堆，共用同一个 `ts` 与 `shop`；字段 `ts / shop / player / item{id,aux,name,nbt}`，`nbt` 是 `stack.save(SaveContext{SaveContext::SaveUseCase::Clone})->toString()`；普通店与管理员店买入方向都不写
- [ ] 两个文件同一套轮转与"写失败只 warn 一次、交易照常成功"的策略；`transactionLog.enabled=false` 时**两个一起停**
- [ ] 单测：`fail` / `frozen` / `frozen_pair` 三种行、`admin` 组合校验、`discarded` 行的序列化（长 SNBT 原样透传、仍是一行）
- [ ] 真机：余额不足一次 → `fail` + `no_money`；管理员店卖一次 → 业务流水一条 `ok` + `discarded` 里对应那件货的 NBT
- [ ] `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 顺序验证：日志里绝不出现「`ok` 之后又跟进一条退款」——`ok` 是终态。
- `discarded` 与业务流水同一套压缩规则（03），不要各写一份。

## Answer

（待实现）
