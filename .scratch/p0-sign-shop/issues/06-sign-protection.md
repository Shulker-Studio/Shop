# 06: 牌面防护

**What to build:** 非店主对商店牌的任何改动都无效：改文本、染料染色、墨囊、荧光墨囊、上蜡、除蜡。店主全部允许。上蜡之后连店主自己也要先除蜡才能改，但交易照常。

**Blocked by:** 03

**Status:** resolved

- [x] 非店主点牌不弹编辑界面（交互阶段就拦下）
- [x] 非店主手持染料点牌 → 颜色不变
- [x] 非店主手持墨囊或荧光墨囊 → 文字样式不变
- [x] 非店主手持蜜蜡 → 牌子不上蜡
- [x] 非店主手持斧头点已上蜡的牌 → 除不了蜡
- [x] 店主执行以上全部操作 → 全部生效
- [x] 上蜡后店主无法直接编辑，除蜡后可编辑
- [x] 上蜡后的商店仍可正常买卖

## Answer

**已实现（`xmake -y` 构建通过，`ShopTests` 148 cases / 2630 assertions 全绿）**

先说结论性的那一件事：03 落地后，交互阶段对**所有**可交互的商店牌都 `cancel()`，所以「非店主的染色/墨囊/蜜蜡/斧头被拦住」在 06 之前就已经成立（前提是取消确实能挡住 `SignBlockActor` 的改动，这条在 12 里）。06 真正要做的是反过来——**给店主开一条通路**，并把「谁能改」从散落的条件收成一处：

- `src/shop/SignProtection.h/.cpp`（新，纯逻辑，不含引擎类型）：
  - `shopActorOf(记录 XUID, 玩家 XUID, 是否 OP)` → `Other / Owner / Operator / NoRecordedOwner`。ADR-0005 的店主判定只写在这里，交互阶段的表单角色与提交阶段的编辑权限都从它派生，不再各写一份 XUID 比较。
  - `canManageShop(actor)`（管理入口，无主牌 fail-closed）/ `mayEditShopSignText(actor)`（改牌面文本，无主牌 fail-open）。
  - `signTextEditRejection(actor, waxed, ownerName, adminShop)`：提交阶段的一道总闸，规矩与两句话都只在这里。
  - `classifySignTouch(物品类型名)` → `None / Dye / InkSac / GlowInkSac / HoneyComb / Axe`。白名单表（16 种染料 + 7 种斧头 + 墨囊/荧光墨囊/蜜蜡）；归一化（小写、去命名空间、只留字母数字）让 `minecraft:red_dye` 与 `Red Dye` 都认。误判成 `None` 只会多弹一次表单，不会开出一条改牌的路。
  - `planSignTouch({touch, actor, sneaking, waxed, facingFront})` → `TakeOver / PassThrough / Block` + 一句提示。交互阶段该不该取消、取消后弹表单还是说话，只有这一处判断。
- `src/mod/SignInteractListener.cpp`：接线。`PassThrough` 不取消（原版染色/上蜡/除蜡/编辑界面照做），`Block` 取消并提示（**不吃连点冷却**，否则一次被拦的染色会把下一次点击吞掉），`TakeOver` 走原来的冷却 + 表单。
- `src/mod/ShopService.cpp`：
  - `inspectShop` 拆成两件事：「这一格是不是商店牌」（正面文本像商店 + 记录的店主，**不看容器**）与「现在能不能交易」（`readShop`，看附着格）。商店箱被拆掉时交易停摆，但牌面保护不跟着停。
  - `planSignEdit` 加提交阶段的闸：改前正面像商店牌 → `signTextEditRejection`（非店主丢包、上蜡的牌连 OP 也丢包）。判据用 `looksLikeShopSign` 而不是 `ok()`：调小 `maxShopAmount`、改 `adminShopName`、外部工具写脏数据，都不该让保护整体失效。
  - 已有商店的编辑里，第 1 行写**原店主的名字**（§11），不再写点击者——OP 代改不改归属，也不能把牌面显示名改成 OP。
- `src/mod/ShopOwner.*`：`parseShopSignFront`（「`editorIsOp` 固定传 true」这个反直觉常量只留一处）；`ensureOwnerRecorded` → `recordShopOwner(..., SignOwnerHandoff)`，归属按「第一次成为商店写给编辑者，之后改牌面不改归属」落地。
- `src/mod/SignEditListener.cpp`：`PlanSignEdit` 在 Before 阶段判出归属交接（改前是不是商店、原 XUID），暂存一条，`PlayerEditSignAfterEvent` 里比对牌位与编辑者后取用。
- `tests/shop/SignProtectionTest.cpp`（新）：22 个中文用例，覆盖物品分类、身份判定、交互决策（含背面、潜行、上蜡、斧头没蜡、分类漏判）、提交闸与文案。

**决策与偏离**

- **店主的编辑入口 = 潜行 + 右键**（与用户确认后定的）：普通右键弹表单，潜行右键放行原版编辑界面。这是 P0 里唯一能改牌面的路（§10.1 的「管理商店」还是占位）。非店主潜行**仍然只弹表单**——潜行不是后门（§17.2 第 2 条也因此改写）。已同步 §9.2 表格、§10.1 的 P0 现状、§13 第 19 条与 20a～20g、ADR-0005 末尾的后续注。
- **物品类操作与「看哪一面」无关**：上蜡是整块牌子（`mIsWaxed`）的属性，站在背面刷一下就能把店主的编辑权限冻住。所以染色/墨囊/上蜡/除蜡的判定只看「这一格是不是商店牌」，不看玩家站在正面还是背面。文字则不同，背面仍是布告板（§5.5）。这是本轮**自己发现并补的洞**（第一版只保护正面）。
- **保护判据不看容器，也不要求「现在严格合法」**：文本闸用「像不像商店牌」+ 记录的 XUID。理由是一个便宜动作（拆箱子 / 改配置 / 写脏数据）不该能关掉保护。
- **上蜡是一道对所有人的闸**：提交阶段上了蜡的牌连 OP 也不放（§9.2 的表格里 OP 与店主同列）。这一条与「你是谁」分开算，所以放在 `signTextEditRejection` 里而不是 `mayEditShopSignText` 里。
- **无主牌（`mLastEditXuid` 为空）三处口径不同，故意的**（§9.2 新增了一张表）：改文本 fail-open（用户确认的口径，代价是被第一个改它的人接手）、管理入口 fail-closed、物品类操作只给 OP。
- **`ensureOwnerRecorded` 从「每次跟上编辑者」改成「按 Before 判出的归属写」**：这是本 ticket 唯一改动 03 交付行为的地方，但不改就会出两个真问题——(1) 06 之后 OP 可以代改别人的牌面，每次跟上编辑者等于“OP 改一下就抢店”；(2) `mLastEditXuid` 每次正面编辑都会变，只看“记录是不是空的”会把别人早先写的普通公告当成店主。现在的规则对「引擎到底写不写 `mLastEditXuid`」两种实测结果都成立（引擎不写我们补，引擎写了我们纠回来）。
- **斧头对没上蜡的牌 = 普通点击**：那一下本来什么都不改，所以对谁都当成普通点击（正面弹表单、背面放行），不再编一句“只有店主能除蜡”的提示。
- **分类只看物品类型名**：引擎侧的类型名与展示名都认，但不依赖 `SignBlock::_getInteractResult`（它只在 `LL_PLAT_C` 下有），免得为“这一次到底会不会真的改色”去摸引擎分支。

**已知边界**

- **「取消到底能不能挡住染色/上蜡」仍未真机验**：`PlayerInteractBlockEvent` 的 `cancel()` 语义与 03/04 的「取消能否阻止开牌/开箱」是同一条风险。挡不住的话，验收第 2～5 条会失守，Plan B 见 12。提交阶段对**文本**有第二道闸，对染色/上蜡/除蜡没有——引擎没有对应的可拦截事件。
- **无主牌会被第一个改它的人接手**：这是 fail-open 的代价（用户确认的口径）。正常情况下不会出现：`recordShopOwner` 在牌子第一次成为商店时就会写入。
- **P0 不能转让商店**：改牌面受店主权限约束，所以 ADR-0005 写的「对方来改这块牌子」这条路在 06 之后不通；换人只能拆牌重开（拆牌是店主权限）。已写进 ADR-0005 的后续注与 §11，转让需要 P1 单独设计。
- **提交阶段这道闸没有自动化测试**：`planSignEdit` 吃 `BlockSource&` / `ServerPlayer&`，纯逻辑部分（`shopActorOf` / `signTextEditRejection` / `mayEditShopSignText`）都测了，剩下的接线只能真机验（12 已列）。
- **归属交接只存一条**：Before → After 之间用一个单槽暂存（牌位 + 编辑者 + 交接内容），取用时比对牌位与编辑者。同一块牌的两个事件是连着来的，没对上的情况（别的模组写牌）就当没存，不动归属。
- **被拦下的提示不节流**：`Block` 路径故意不吃连点冷却（否则会吞掉紧接着的那次点击），代价是拿蜜蜡连点会被刷屏。
- **物品名表要跟着游戏版本走**：新材质斧头 / 新染料不会自动认出来（会退化成 `None` → 店主多弹一次表单）。方向是安全的，但需要跟着版本补表。

**指标**

- `xmake -y`：Shop.dll 构建通过（clang-cl，`/W4` 无新增告警）。
- `xmake build ShopTests` + `build/windows/x64/release/ShopTests.exe`：148 cases / 2630 assertions 全绿（06 之前 126 / 2507，只增不减）。
- `xmake test`：1 个目标通过。
- `git diff --check`：无空白问题。

**审查后修正（code-review 两轴，两轮）**

第一轮（Standards 4 条必须改 + Spec 7 条）：

- **背面是个洞**（Standards 轴提出，Spec 轴同）：第一版只保护正面，`mIsWaxed` 却是整块牌子的属性——站到背面刷蜜蜡就能冻住店主的编辑。现在物品类操作与看哪一面无关，并补了背面用例与 §13 20c。
- **「谁是店主」写了两遍**（Standards 轴）：`inspectShop` 里一份 `!ownerXuid.empty() && == viewer`、`mayEditShopSignText` 里另一份，且对空 XUID 给出相反答案。收成 `shopActorOf` + 两个具名策略（`canManageShop` / `mayEditShopSignText`）。
- **`existingIsShopSign` 参数在生产路径上恒为 true**（Standards 轴）：删掉，判据交给调用方的 `looksLikeShopSign`。
- **`Block` 吃掉连点冷却**（两轴都提）：把 `Block` 移到 `TradeGate` 之前。
- **提交阶段不看 waxed**（Spec 轴）：补上第二道闸，并抽出 `signTextEditRejection` 让规矩可单测。
- **OP 改一次别人的牌面就把归属换成 OP**（Spec 轴，最重的一条）：`recordShopOwner` 改成按归属规则写回，见上面的「决策与偏离」。
- **提示文案的句式手写了 6 遍 / 术语与 GLOSSARY 对齐**：收成 `ownerOnlySentence`；管理员商店单独一句；§9.3 的引用改成 §9.2。
- **文档错别字与漏项**：`兑底`→`兜底`、`按跘`→`按下`、用例数对不上、§6.3/§12 未同步。

第二轮（Standards 6 条必须改 + Spec 10 条）：

- **无主牌的 fail-open 在正常玩法里走不到**（两轴都提）：`planSignTouch` 原来看 `canManageShop`（fail-closed），潜行到正面只弹表单，提交阶段的 fail-open 成了死分支。现在「能不能这样进编辑界面」问的是 `mayEditShopSignText`——与提交阶段同一个判据。
- **`ensureOwnerRecorded` 的「只补空」会把早先的普通编辑当成店主**（Spec 轴）：改成 Before → After 的归属交接。
- **OP 代改后第 1 行被写成 OP 的名字**（Spec 轴）：已有商店的编辑现在写原店主的显示名。
- **判据用 `ok()`，配置一变闸门整体消失**（Spec 轴）：改用 `looksLikeShopSign`。
- **两阶段对「是不是商店牌」判据不同**（Spec 轴）：交互阶段也改成「文本 + 记录的店主」，容器只决定能不能交易。
- **注释/文档与实现不符、`ShopInteraction` 的默认值方向错、聚合初始化漏字段、`<cstddef>` 缺失、物品表注释夸张、两个自证的文案用例**：逐条改掉；两个文案用例换成有区分度的断言（五句话互不相同、管理员商店那句必须提 OP）。

**未采纳**

- **给 `Block` 的提示加节流**：会重新引入「一次被拦的点击吞掉下一次点击」，而刷屏需要故意连点。
- **把物品名表暴露给测试**（测试遍历实现里的表）：16 种染料 / 7 种斧头是领域事实，测试里那份列表就是规格；把表变成公开 API 只是为了测试，不划算。
- **把 `src/mod/ShopService.cpp` 里的 `messageFor` 也搬进 `src/shop`**：03 的文案散在 mod 层是既有事实，本票只保证新文案在纯逻辑层；为统一而搬会动到 03 的交付面。
- **上蜡/除蜡也走“提交阶段兜底”**：引擎没有染色/上蜡的提交事件可拦，只有文本有 `PlayerEditSignBeforeEvent`。
- **用物品类别（`ItemStack` 的 tag / 工具类型）代替名字表**：引擎侧的物品类别接口在本版本没验证过，名字表至少是白名单、方向安全。

**只能真机验收的项（已转 12）**

1. 非店主拿染料/墨囊/荧光墨囊/蜜蜡/斧头点牌 → 牌子一个像素都不变（`cancel()` 到底挡不挡得住 `SignBlockActor` 的改动）。
2. 店主拿同样四类物品点牌 → 染色/样式/上蜡/除蜡真的生效（`PassThrough` 交回原版是否走通）。
3. 潜行 + 右键是否真的弹原版编辑界面（店主/OP）；若潜行在基岩版挡掉方块交互，Plan B 是换手势或改走管理表单。
4. 上蜡后店主潜行右键 → 我们的「先拿斧头除蜡」提示；除蜡后再潜行右键 → 能编辑并保存。
5. `SignBlockActor::mIsWaxed` 的实际取值（新牌 false / 刷蜜蜡 true / 斧头除蜡后 false）。
6. `ItemStack::getTypeName()` 的返回值形态，以及 16 种染料 + 7 种斧头是否都被分类器认出。
7. `mLastEditXuid` 是否被包路径写入 → 决定归属交接的哪一半生效（引擎写了我们纠回来，没写我们补上）。
8. A 写普通公告、B 改成商店 → 店主是 B；OP 代改一次别人的商店 → 店主与第 1 行都不变。
9. 上蜡后的商店仍可正常买卖（表单照弹、交易链路不受 `waxed` 影响）。
10. 非店主从**背面**刷蜜蜡 → 被拦（上蜡是整块牌子的事）。
