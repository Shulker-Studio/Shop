# 02: 管理员店卖出无限 + 销毁留档

**What to build:** 玩家卖货给管理员店永远成功，不会因为"商店箱满了"被拒；交出去的货**销毁**（不写进箱子，样品槽一件不动），同时把被销毁的物品完整 NBT 交给留档通道。

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] 卖出方向的份数上限里**去掉"商店箱空间"这一项**：货是销毁的，永远收得下；其余上限（玩家手里的货、店主余额、`maxShopAmount`）照旧生效
- [ ] `ShopInfoData::boxRoomUnits` 也得能表达"不限制"（现在是个 `int`，用 01 引入的 `StockView` 或 `optional<int>` 都行），
      否则表单会显示「商店箱还能装 0 份」并把上限夹成 0
- [ ] 卖出执行路径：给钱 → 从背包取货 → 销毁（不写容器）→ 完成；样品槽与其余格子都保持原样
- [ ] 销毁前把「被销毁的物品堆」交给流水的 `discarded` 通道。**取 SNBT 的正确写法**：`stack.save(SaveContext{SaveContext::SaveUseCase::Clone})->toString()`（`ItemStack` 自己没有 `save(CompoundTag&)`）；留档在 `Done` 之前生成、`Done` 之后落盘，留档失败只 `warn`，**不许**因此把交易判失败
- [ ] 补偿必须对称：钱记上了但取货失败 → 把已取走的物品还给玩家；任一补偿失败 → 与 P0 同一套（`fatal` + 通知 OP + 冻结这一对）
- [ ] 表单正文别写「商店箱还能装 0 份」这类误导数字
- [ ] 单测：无限收购下箱空间不参与上限；玩家手里没有货仍被拒（`NotEnoughGoods`）；店主余额不足仍被拒；普通店卖出行为不变
- [ ] 真机：卖 10 次 → 每次都收下并给钱、箱子一件不动（第一格逐字段不变）、`logs/discarded-*.jsonl` 里能 `jq` 出对应的 `item.nbt`
- [ ] `xmake test` 全绿，测试数与断言数写进本票

## Answer

（待实现）
