# 01: 管理员店买入无限（卖不完）

**What to build:** 在一家管理员店里连买 10 次，箱子里的样品一件不少、这家店永远有货。这一票把「库存 = 无限」从模型一路打通到执行路径：份数上限不再被库存卡住、表单写「库存：无限」、买的时候不从箱子里扣。做完就能真机验收（买家一直买得到，箱子数量不变）。

**Blocked by:** None (can start immediately)

**Status:** ready-for-agent

- [ ] 纯逻辑引入 `StockView { std::optional<int> units; bool unlimited; }`，`makeFinite(int)` / `makeUnlimited()` 两个构造，`unlimited && units` 视为构造错误；**禁止**用 `INT_MAX` / `-1` 之类哨兵参与 `min`
- [ ] **"无限"要一次改干净三处**（`rg 'stockUnits|goodsUnits|roomUnits'` 应全命中）：① `TradePlan` 的 `TradeRequest` 与 `maxTradeUnits`（两项改成 `StockView` 或 `optional<int>`，无限时跳过 `min` 与 `<= 0 就拒`）；② `TradePricing::tradeUnits`（是另一套 min，漏改就会一边说无限、一边被卡住）；③ `UnitsForm` 里按 `stockUnits` 算"还剩几份"的那段
- [ ] `ShopInfoData::stockUnits` 也跟着换成 `StockView`（否则 `planTrade` 认为无限、表单还显示数字，两套真相）
- [ ] 管理员店（`adminShop`）的**商店箱库存**一律 `makeUnlimited()`；**本票只让买入方向生效**
- [ ] 别顺手改错字段：卖出方向的"货"来自玩家背包（`heldUnits`），**不是** `stockUnits`——它必须保持有限，
      否则玩家能无限卖自己有的东西（那是复制物品）。`rg heldUnits` 确认它没被无限化
- [ ] 买入执行路径：不调用容器写入，样品槽数量前后完全一致；失败（背包满）照旧退款并提示
- [ ] 表单正文写「库存：无限（管理员店，永远有货）」，不编数字；其余三个上限（余额、`maxShopAmount`、背包空间）在无限下仍然生效
- [ ] 单测：无限买 1 / 2 / 4 / 自定义；余额不足仍被拦；单次上限仍生效；背包空间仍生效；`maxTradeUnits` 在无限时不看库存项；
      普通店行为完全不变（现有 `TradePlanTest` / `TradePricingTest` 断言**一条都不许改**，改了就说明你在悄悄改普通店行为）
- [ ] 真机：管理员店买 10 次 → 每次都给货、都能付钱、箱子内数量一件不少（顺带看 `mtrans` 一笔一条）
- [ ] `xmake build Shop` / `xmake build ShopTests` / `xmake test` 全绿，测试数与断言数写进本票

## 备注

- 卖出方向**这一票先不动**（仍按有限库存跑），02 再把「收得下」也变成无限。
- 模板箱空（没有任何非空槽位）不归 `planTrade` 管，那是 03 的独立原因；别把两条判据混进一个函数。
- DESIGN `§10.1` / `§10.3` 的公式段要补一句「库存为无限时该项不参与 min」。

## Answer

（待实现）
