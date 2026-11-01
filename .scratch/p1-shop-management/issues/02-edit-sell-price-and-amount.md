# 02: 管理表单里的卖出价与每份件数

**What to build:** 在 01 打通的那条链路上，再加两个入口：「改卖出价」（留空 = 不收购）与「改每份件数」（1..`maxShopAmount`）。提交后牌面第 2、3 行按同一个规范化函数更新，所有人立刻看到。

**Blocked by:** 01

**Status:** resolved

- [x] 「改卖出价」输入框初值 = 当前卖出价，留空 = 不收购（第 3 行只剩 `B 10`）
- [x] 「改每份件数」输入框初值 = 当前件数，范围 `1..maxShopAmount`，越界给原因（「一份的件数要写 1 到 N」），**不悄悄夹到边界**
- [x] **两侧价格都清空必须拒绝**（spec D6）：第 3 行一空，`looksLikeShopSign` 为假 → 牌与箱子的保护全部失效。提示写「至少留一侧价格，否则这块牌就不再是商店牌（箱子会失去保护）」，并说明「想关店」是拆牌或停业
- [x] `blockSellPriceHigherThanBuy` 仍然生效（卖出价高于买入价 → 拒绝并说明是哪条规则）
- [x] 三次改动共用 01 的合成函数与写牌函数，**不许**各写一份格式化或各写一次发包
- [x] 改动只影响第 2、3 行：第 1 行（店主名）与第 4 行（商品名）一个字不动
- [x] 单测：卖出价留空、卖出价正常改、件数边界 1 / `maxShopAmount` / 0 / `maxShopAmount+1`、两侧都空被拒、`S > B` 被拒、改完之后 `formatShopSign` 的输出与手写编辑同格式
- [ ] 真机：改完卖出价与件数后买卖各一次（**未验**，见 ## Answer 的「未验」表第 ④ 条），金额与件数按新的来（旧牌不用重建）
- [x] `xmake test` 全绿，测试数与断言数写进本票

## Answer

**2026-11-01 实现完毕（待提交）。**

### 落地的东西

**纯逻辑（`src/shop`，无 `mc/` 头）**

- `ManageForm.{h,cpp}`：
  - `withSellPrice(current, input, config)` 与 `withAmount(current, input, config)`，与 `withBuyPrice` 同样返回 `ManageEdit`。
  - 买卖两侧共用同一套内部实现：`parsePriceInput` + `withPrice`（只换「落到哪一侧」与提示里的名字），
    收尾校验统一走 `validatedEdit`（两侧都清空拒绝、`S > B` 拒绝）。规则与措辞各只有一处。
  - 件数不复写文法：直接问牌面第 2 行那份 `parseAmountLine`（表单只是先 `trimCopy`，与买卖价一致），
    所以「什么叫合法件数」与手写编辑只有一份；范围文案由新的 `amountOutOfRangeMessage`（`ShopSign`）
    供手写编辑（`ShopService::messageFor`）与管理表单共用。空、非整数、`0`、`maxShopAmount + 1`、溢出
    一律给「一份的件数要写 1 到 N 之间的整数」，**不夹到边界**。
  - 件数不跑 `validatedEdit`：它一个字段都不碰价，改完造不出「两侧都空」或 `S > B`（而当前牌已经过了
    `parseShopSignText`）。价格那三条校验因此仍只有一处。
  - `kBothEmptyReason` 补一句「想关店请拆牌，或者停业（牌子与库存都留着）」——spec D6 要求的「想关店」正解。
  - `shopManageFormButtons` 变三个入口，文案带当前值：`改买入价（现在 $ 10 / 份）`、`改卖出价（现在 $ 5 / 份）`、
    `改每份件数（现在 64 个）`；某一侧没有价时写「不出售」/「不收购」，不显示假价格。

**BDS 适配（`src/mod`）**

- `ShopForm.{h,cpp}`：
  - `requestBuyPriceForm` 换成 `requestManageInputForm(viewer, sign, action)`，`action` 只收三个「改一项」的动作。
  - `manageInputField(action, data, signConfig)` 是这三个入口的唯一一张表：控件名、标题后缀、输入框标签、
    占位提示、初值，以及一个 `ManageChange`（合成函数 + 成功提示）。三张 `CustomForm` 与三条提交链因此各只有一份：
    `openManageInputForm` 只负责渲染，`submitManageEdit` 只接 `ManageChange`，负责
    「重新取牌 → 重新 `parseShopSignText` → 重新 `inspectManageTarget` 鉴权 → `formatShopSign` →
    `setMessageForServerScripingOnly`（普通店店主 XUID / 管理员店空串，第 1、4 行沿用原文）→
    `resyncBlockActorToDimension`」。
  - `onManageButton` 删掉，管理表单的按钮与第一张表单走同一个 `onButton`；`ManageSellPrice` / `ManageAmount`
    不再在 switch 里占位返回。
  - 输入读回仍只走 `get_if<std::string>`，读不到报「读不到这个输入框的值」，不当 0、也不当留空。

**文档**：`DESIGN.md` §10.1 增「P1 现状（票 02 落地后）」；`CHANGELOG.md` 增本票四条。

### 审查（code-review skill，两轴并行）

- **Standards 轴**（`git diff HEAD`）报了 3 条硬性与 7 条判断题，均已在提交前修掉：
  - 硬性三条：CHANGELOG 写「件数也走 `parsePriceInput`」而代码是自写 `from_chars`（已改成复用 `parseAmountLine`）；
    `ManageForm.h` 把「三条校验只在这一处」挂在 `withBuyPrice` 上（已改成指实现在 `withPrice` / `validatedEdit`）；
    件数范围文案与 `ShopService::messageFor(BadAmount)` 逐字重复（已抽成 `ShopSign` 的 `amountOutOfRangeMessage`，两处共用）。
  - 判断题采纳：两处 `trim` + `from_chars` 合一（件数改用 `parseAmountLine` 后只剩一处）；
    `checkedEdit` 改名 `validatedEdit`（名字要揭示校验的是什么）；`ManageFieldForm` → `ManageInputField`；
    参数 `field` → `action`；`submitManageEdit` 不再为了两个函数指针把 placeholder 等文案构造一遍（多出 `ManageChange`）；
    `priceButtonLabel` 的参数从整个 `ShopInfoData` 收窄到 `currencySymbol`；买卖清空提示按侧区分（「不再出售」/「不再收购」）。
  - 未采纳「`manageInputField` 的 `ShopFormAction` switch 重复」一条：`onButton` 是动作分发，`manageInputField` 是
    数据表，两者本来就该一起改；硬合成一个 switch 反而要把表单文案塞进分发器里。
- **Spec 轴**（并行 subagent，对照 `spec.md` + 本票逐条核对）：票面清单全部「做到」，无 03 越界（`ToggleClosed` 仍早退、
  没有 `:x`）。审查提了三点，两点已采纳：件数文法复用 `parseAmountLine`（spec「两条写入路径共用同一套解析与规范化」）、
  改件数不再跑价格校验；一点**保留**——两侧都空的提示里提「停业」：03 的入口还没落地，但这句是 spec D6 的固定口径
  （「想关店的正确做法是拆牌或者停业」），03 落地后不用再改文案。

### 验证

```
xmake build Shop        # ✅ 无 error / 无 warning
xmake build ShopTests   # ✅
xmake test              # ✅ 1/1 passed
ShopTests.exe           # 240 test cases / 3131 assertions，全过
```

基线 `224 cases / 3019 assertions`，本票净增 **16 个用例 / 112 条断言**（`tests/shop/ManageFormTest.cpp`：
新增 17 个、删 1 个（「本票只有『改买入价』」被三个按钮的用例取代））。

### 未验（只能真机）与退路

| 项 | 状态 | 验不过的退路 |
| --- | --- | --- |
| ① `setMessageForServerScripingOnly` 写 `mLastEditXuid` | 未验（票 01 遗留） | 自己补写 `mLastEditXuid` |
| ② 改动能落盘（重启后还在） | 未验（票 01 遗留） | 写完显式 `SignBlockActor::onChanged(region)` |
| ③ 上蜡的牌能被这条路径写入 | 未验（票 01 遗留） | 判 `mIsWaxed` 给明确提示，不硬写 |
| ④ 改完卖出价与件数后买卖各一次，金额与件数按新的来（旧牌不用重建） | 未验 | 若交易仍按旧值，先查 `resyncBlockActorToDimension` 与牌面解析时机（§19.3）；若客户端显示仍旧，退路同 ② |

本次只做构建与单测，**没有真机验证**；也没有为了让测试过而改生产逻辑。
