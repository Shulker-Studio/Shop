# 06: 牌面防护

**What to build:** 非店主对商店牌的任何改动都无效：改文本、染料染色、墨囊、荧光墨囊、上蜡。店主全部允许。上蜡之后**谁都改不了牌面文本**（原版不能除蜡，2026-11-01 修正，见文末），但交易照常。

**Blocked by:** 03

**Status:** resolved

- [x] 非店主点牌不弹编辑界面（交互阶段就拦下）
- [x] 非店主手持染料点牌 → 颜色不变
- [x] 非店主手持墨囊或荧光墨囊 → 文字样式不变
- [x] 非店主手持蜜蜡 → 牌子不上蜡
- [x] 非店主手持斧头点已上蜡的牌 → 除不了蜡
- [x] 店主执行以上全部操作 → 全部生效
- [x] 上蜡后店主也无法直接编辑（蜡不可逆，只能拆牌重立）
- [x] 上蜡后的商店仍可正常买卖

## Answer

**已实现（`xmake -y` 构建通过，`ShopTests` 152 cases / 2652 assertions 全绿）**

先说结论性的那一件事：03 落地后，交互阶段对**所有**可交互的商店牌都 `cancel()`，所以「非店主的染色/墨囊/蜜蜡/斧头被拦住」在 06 之前就已经成立（前提是取消确实能挡住 `SignBlockActor` 的改动，这条在 12 里）。06 真正要做的是反过来——**给店主开一条通路**，并把「谁能改」从散落的条件收成一处：

- `src/shop/SignProtection.h/.cpp`（新，纯逻辑，不含引擎类型）：
  - `SignOwnerHandoff::keepsPreviousOwner(新的是不是管理员店)`：一次编辑该不该保住原店主（改前是同一家商店、有记录、管理员性质没变）。第 1 行写谁与归属写谁共用它。
  - `shopActorOf(记录 XUID, 玩家 XUID, 是否 OP)` → `Other / Owner / Operator / NoRecordedOwner`。ADR-0005 的店主判定只写在这里，交互阶段的表单角色与提交阶段的编辑权限都从它派生，不再各写一份 XUID 比较。
  - `canManageShop(actor)`（管理入口，无主牌 fail-closed）/ `mayEditShopSignText(actor)`（改牌面文本，无主牌 fail-open）。
  - `signTextEditRejection(actor, waxed, ownerName, adminShop)`：提交阶段的一道总闸，规矩与两句话都只在这里。
  - `classifySignTouch(物品类型名)` → `None / Dye / InkSac / GlowInkSac / HoneyComb / Axe`。白名单表（16 种染料 + 7 种斧头 + 墨囊/荧光墨囊/蜜蜡）加形状兜底（`*_dye`、`*_axe` 排除镐）；归一化（小写、去命名空间、只留字母数字）让 `minecraft:red_dye` 与 `Red Dye` 都认。漏判成 `None` 时：**正面**只是多弹一次表单（仍然 cancel），**背面**等于放行——所以才有形状兜底，剩下的漏判写在已知边界里。
  - `planSignTouch({touch, actor, sneaking, waxed, facingFront})` → `TakeOver / PassThrough / Block` + 一句提示。交互阶段该不该取消、取消后弹表单还是说话，只有这一处判断。
- `src/mod/SignInteractListener.cpp`：接线。`PassThrough` 不取消（原版染色/上蜡/编辑界面照做），`Block` 取消并提示（**不吃连点冷却**，否则一次被拦的染色会把下一次点击吞掉），`TakeOver` 走原来的冷却 + 表单。
- `src/mod/ShopService.cpp`：
  - `inspectShop` 拆成两件事：「这一格是不是商店牌」（正面文本像商店 + 记录的店主，**不看容器**）与「现在能不能交易」（`readShop`，看附着格）。商店箱被拆掉时交易停摆，但牌面保护不跟着停。
  - `planSignEdit` 加提交阶段的闸：改前正面像商店牌 → `signTextEditRejection`（非店主丢包、上蜡的牌连 OP 也丢包）。判据用 `looksLikeShopSign` 而不是 `ok()`：调小 `maxShopAmount`、改 `adminShopName`、外部工具写脏数据，都不该让保护整体失效。
  - 已有商店的编辑里，第 1 行写**原店主的名字**（§11），不再写点击者——OP 代改不改归属，也不能把牌面显示名改成 OP。
- `src/mod/ShopOwner.*`：`parseShopSignFront`（「`editorIsOp` 固定传 true」这个反直觉常量只留一处）；`ensureOwnerRecorded` → `recordShopOwner(..., SignOwnerHandoff)`，归属按「第一次成为商店写给编辑者，之后改牌面不改归属」落地。
- `src/mod/SignEditListener.cpp`：`PlanSignEdit` 在 Before 阶段判出归属交接（改前是不是商店、原 XUID），暂存一条，`PlayerEditSignAfterEvent` 里比对牌位与编辑者后取用。
- `tests/shop/SignProtectionTest.cpp`（新）：26 个中文用例，覆盖物品分类（含形状兜底）、身份判定（含管理员店）、归属交接判据、交互决策（含背面、潜行、上蜡、斧头没蜡、分类漏判）、提交闸与文案。

**决策与偏离**

- **店主的编辑入口 = 潜行 + 右键**（与用户确认后定的）：普通右键弹表单，潜行右键放行原版编辑界面。这是 P0 里唯一能改牌面的路（§10.1 的「管理商店」还是占位）。非店主潜行**仍然只弹表单**——潜行不是后门（§17.2 第 2 条也因此改写）。已同步 §9.2 表格、§10.1 的 P0 现状、§13 第 19 条与 20a～20g、ADR-0005 末尾的后续注。
- **物品类操作与「看哪一面」无关**：上蜡是整块牌子（`mIsWaxed`）的属性，站在背面刷一下就能把店主的编辑权限冻住。所以染色/墨囊/上蜡的判定只看「这一格是不是商店牌」，不看玩家站在正面还是背面。文字则不同，背面仍是布告板（§5.5）。这是本轮**自己发现并补的洞**（第一版只保护正面）。
- **保护判据不看容器，也不要求「现在严格合法」**：文本闸用「像不像商店牌」+ 记录的 XUID。理由是一个便宜动作（拆箱子 / 改配置 / 写脏数据）不该能关掉保护。
- **上蜡是一道对所有人的闸**：提交阶段上了蜡的牌连 OP 也不放（§9.2 的表格里 OP 与店主同列）。这一条与「你是谁」分开算，所以放在 `signTextEditRejection` 里而不是 `mayEditShopSignText` 里。
- **无主牌（`mLastEditXuid` 为空）三处口径不同，故意的**（§9.2 新增了一张表）：改文本 fail-open（用户确认的口径，代价是被第一个改它的人接手）、管理入口 fail-closed、物品类操作只给 OP。
- **`ensureOwnerRecorded` 从「每次跟上编辑者」改成「按 Before 判出的归属写」**：这是本 ticket 唯一改动 03 交付行为的地方，但不改就会出两个真问题——(1) 06 之后 OP 可以代改别人的牌面，每次跟上编辑者等于“OP 改一下就抢店”；(2) `mLastEditXuid` 每次正面编辑都会变，只看“记录是不是空的”会把别人早先写的普通公告当成店主。现在的规则对「引擎到底写不写 `mLastEditXuid`」两种实测结果都成立（引擎不写我们补，引擎写了我们纠回来）。
- **斧头 = 普通物品**（2026-11-01 修正后）：原版告示牌没有「斧头除蜡」，所以拿斧头点牌对谁都是普通点击（正面弹表单、背面放行）。
- **分类只看物品类型名**：引擎侧的类型名与展示名都认，但不依赖 `SignBlock::_getInteractResult`（它只在 `LL_PLAT_C` 下有），免得为“这一次到底会不会真的改色”去摸引擎分支。

**已知边界**

- **「取消到底能不能挡住染色/上蜡」仍未真机验**：`PlayerInteractBlockEvent` 的 `cancel()` 语义与 03/04 的「取消能否阻止开牌/开箱」是同一条风险。挡不住的话，验收第 2～5 条会失守，Plan B 见 12。提交阶段对**文本**有第二道闸，对染色/上蜡没有——引擎没有对应的可拦截事件。
- **无主牌会被第一个改它的人接手**：这是 fail-open 的代价（用户确认的口径）。正常情况下不会出现：`recordShopOwner` 在牌子第一次成为商店时就会写入。
- **P0 不能转让商店**：改牌面受店主权限约束，所以 ADR-0005 写的「对方来改这块牌子」这条路在 06 之后不通；换人只能拆牌重开（拆牌是店主权限）。已写进 ADR-0005 的后续注与 §11，转让需要 P1 单独设计。
- **提交阶段这道闸没有自动化测试**：`planSignEdit` 吃 `BlockSource&` / `ServerPlayer&`，纯逻辑部分（`shopActorOf` / `signTextEditRejection` / `mayEditShopSignText`）都测了，剩下的接线只能真机验（12 已列）。
- **归属交接只存一条**：Before → After 之间用一个单槽暂存（牌位 + 编辑者 + 交接内容），取用时比对牌位与编辑者。同一块牌的两个事件是连着来的，没对上的情况（别的模组写牌）就当没存，不动归属。
- **被拦下的提示不节流**：`Block` 路径故意不吃连点冷却（否则会吞掉紧接着的那次点击），代价是拿蜜蜡连点会被刷屏。
- **物品名表要跟着游戏版本走**：白名单 + 形状兜底（`*_dye` / `*_axe`）已经覆盖了「新颜色、新材质」这类变化，但改了命名习惯的物品（不是以 dye/axe 结尾、也不是墨囊/蜜蜡）仍会退化成 `None`：正面只会多弹一次表单，**背面等于放行**。真机验收里会逐一点过现有 16 种染料与 7 种斧头。

**指标**

- `xmake -y`：Shop.dll 构建通过（clang-cl，`/W4` 无新增告警）。
- `xmake build ShopTests` + `build/windows/x64/release/ShopTests.exe`：152 cases / 2652 assertions 全绿（06 之前 126 / 2507，只增不减）。
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
- **注释/文档与实现不符、`ShopInteraction` 的默认值方向错、聚合初始化漏字段、`<cstddef>` 缺失、物品表注释夸张、两个自证的文案用例**：逐条改掉；两个文案用例换成有区分度的断言（各句不空且染色/上蜡/上蜡后各说各的、管理员商店那句必须提 OP）。

第三轮（终审，Standards 2 条必须改 + Spec 4 条）：

- **「保住原店主」判了两次、两个信号**（Standards 轴）：第 1 行看「显示名非空 + 不是管理员店」、归属看「XUID 非空」，异常牌上会分叉出「第 1 行是 A、店主是 B」。现在两边都调 `SignOwnerHandoff::keepsPreviousOwner`。
- **管理员店 ⇄ 普通店互转的归属**（Spec 轴）：管理权只给 OP（§11），互转算换了家店，`keepsPreviousOwner` 要求管理员性质不变；否则 OP 把 A 的店改成管理员店后，A 仍被当成店主拿到管理入口。
- **单槽交接会被任意正面 After 事件吃掉**（Spec 轴）：一次无关的编辑就会把待用的交接清掉，引擎没写 XUID 的牌子会静默退化成无主牌（然后谁都能接手）。改成「先比对牌位（含维度）与编辑者，匹配才消费」。
- **未识别物品名在背面能绕过保护**（Spec 轴）：分类器漏判时正面只是多弹一次表单，背面却是放行。加了形状兜底（`*_dye`、`*_axe` 排除镐），并把「漏判的方向是安全的」这句话改准。
- **牌还在但附着格不是容器时，非店主会看到原版编辑界面**（Spec 轴）：改成非店主拦下 + 提示「这家店暂时用不了」，店主与 OP 仍然放行（店坏了要能修）。
- **`ShopOwner.h` 反向依赖 `ShopService.h`**（Standards 轴）：`SignOwnerHandoff` 挪进 `shop/SignProtection.h`（纯数据 + 一个判据，可单测），依赖方向恢复 `mod → shop`。
- **测试 helper 又用了一串 bool**（Standards 轴）：改成 `touch(物品名, SignTouchContext)`；另补分类兜底、归属交接判据、提示互不相同三组用例（22 → 25）。
- **文档事实错误**（Standards 轴）：CHANGELOG 里写成 `ShopSnapshot` 新增 `waxed`（实际在 `ShopInteraction`）；§5.1 第 1 行规则、§3 术语表的「店主」、§4.2 的「提交阶段兜底」、§18 D29 都还是 06 之前的说法，一并同步。

第四轮（终审复核，两条都是「没有阻塞项」；把剩下的确定性偏差清掉）：

- **管理员商店的身份**（Spec 轴）：`shopActorOf` 加上 `adminShop` 入参——管理员店只有**现任 OP** 算管理身份。之前牌上记的创建者被撤权后仍是 `Owner`，会拿到管理入口并改得动管理员店，与 §11 和「管理员商店只有 OP 能改」的文案冲突。
- **停摆原因混成一个**（Spec 轴）：`!shop` 原来把「箱子没了」和「牌面已不严格合法」混在一起，后者会对着好好的箱子说「商店箱不在了」。现在 `ShopInteraction::unavailable` 分开两种原因、两句文案，§9.2 的例外段同步。
- **同一件事两把尺子**（Spec 轴）：停摆时「谁能进原版编辑界面」原来问 `canManageShop`（fail-closed），与潜行路径的 `mayEditShopSignText`（fail-open）不一致。现在两处都问后者，与提交阶段同源。
- **`readShop` 还在自己拼 `parseShopSignText(..., true, ...)`**（Spec 轴）：改用 `parseShopSignFront`，「反直觉常量只留一处」这才成立。
- **单槽交接会被穿插顶掉**（Spec 轴）：两个玩家同一 tick 各改各的牌时，前一个人的交接会被顶掉、那条 After 什么也不写（引擎没写 XUID 的牌子静默变无主牌）。改成 4 条小环，从新到旧找匹配的那条。
- **测试里的死变量与用例名**（Standards 轴）：删掉没人用的 `touches`，用例名与断言对齐；补一组「管理员商店只归 OP」的身份用例（26 个）。
- **文档小偏差**：`isShopSignFront` → 注释改成实际使用的 `parseShopSignFront(...).ok()`；「失败即放行」的出处从 §9.3 改成 §9.2；CHANGELOG 的潜行句补「背面照旧人人都能写」；`<cstddef>` 显式包含。

**未采纳**

- **给 `Block` 的提示加节流**：会重新引入「一次被拦的点击吞掉下一次点击」，而刷屏需要故意连点。
- **把物品名表暴露给测试**（测试遍历实现里的表）：16 种染料 / 7 种斧头是领域事实，测试里那份列表就是规格；把表变成公开 API 只是为了测试，不划算。
- **把 `src/mod/ShopService.cpp` 里的 `messageFor` 也搬进 `src/shop`**：03 的文案散在 mod 层是既有事实，本票只保证新文案在纯逻辑层；为统一而搬会动到 03 的交付面。
- **染色/上蜡没有“提交阶段兜底”**：引擎没有染色/上蜡的提交事件可拦，只有文本有 `PlayerEditSignBeforeEvent`。
- **用物品类别（`ItemStack` 的 tag / 工具类型）代替名字表**：引擎侧的物品类别接口在本版本没验证过，名字表至少是白名单、方向安全。

**只能真机验收的项（已转 12）**

1. 非店主拿染料/墨囊/荧光墨囊/蜜蜡/斧头点牌 → 牌子一个像素都不变（`cancel()` 到底挡不挡得住 `SignBlockActor` 的改动）。
2. 店主拿同样四类物品点牌 → 染色/样式/上蜡真的生效（`PassThrough` 交回原版是否走通）。
3. 潜行 + 右键是否真的弹原版编辑界面（店主/OP）；若潜行在基岩版挡掉方块交互，Plan B 是换手势或改走管理表单。
4. 上蜡后店主潜行右键 → 提示「上了蜡，只能拆牌重立」；斧头点牌只是普通点击，蜡不会消失。
5. `SignBlockActor::mIsWaxed` 的实际取值（新牌 false / 刷蜜蜡 true / 斧头点过后仍是 true）。
6. `ItemStack::getTypeName()` 的返回值形态，以及 16 种染料 + 7 种斧头是否都被分类器认出。
7. `mLastEditXuid` 是否被包路径写入 → 决定归属交接的哪一半生效（引擎写了我们纠回来，没写我们补上）。
8. A 写普通公告、B 改成商店 → 店主是 B；OP 代改一次别人的商店 → 店主与第 1 行都不变。
9. 上蜡后的商店仍可正常买卖（表单照弹、交易链路不受 `waxed` 影响）。
10. 非店主从**背面**刷蜜蜡 → 被拦（上蜡是整块牌子的事）。
11. **引擎写 `mLastEditXuid` 的时序**：如果它在 `PlayerEditSignAfterEvent` **之后**才落盘，我们的纠偏会被它覆盖 → OP 代改仍会把归属写成 OP。真机上第一次测「OP 代改」就要顺手看一眼牌面归属是原店主还是 OP；若是后者，需要在 After 之后延后一拍再纠一次。
12. 提交阶段的兜底本身：非店主把正面文本改到一半（例如拆掉商店箱让表单弹不出来、再从原版编辑界面提交）→ 收到「这块商店牌是 X 的」且牌面一个字没变；店主同样的操作能保存。
