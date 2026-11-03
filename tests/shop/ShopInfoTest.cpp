#include "shop/ManageForm.h" // shopManageFormButtons
#include "shop/ShopInfo.h"
#include "shop/ShopSign.h" // stripColorCodes： 断言时去掉颜色码
#include "../TextHelper.h"

#include <algorithm>
#include <doctest/doctest.h>
#include <string>

namespace {

using namespace ss::shop;

/// 一个典型的普通商店： 钻石，64 个一份，买入 10、卖出 5，可成交 3 份。
ShopInfoData sampleShop() {
    ShopInfoData data;
    data.ownerName     = "Alex";
    data.itemName      = "钻石";
    data.amountPerUnit = 64;
    data.buyPrice      = 10;
    data.sellPrice     = 5;
    data.stockUnits    = 3;
    // 货币符号只从 `economy.currencySymbol` 来，这里手填模拟调用方
    data.currencySymbol = "$";
    return data;
}

/// 按钮列表里有没有某个文案（按玩家看到的那段字比）。
bool hasButton(std::vector<ShopFormButton> const& buttons, std::string const& label) {
    return std::any_of(buttons.begin(), buttons.end(), [&](ShopFormButton const& button) {
        return plainText(button.label) == label;
    });
}

/// 断言的永远是**去掉颜色码之后**的文本： 颜色是外观，不该把每个测试都钉死在 `§7` 上。
bool hasLine(std::vector<Text> const& lines, std::string const& line) {
    for (auto const& candidate : lines) {
        if (plainText(candidate) == line) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("盒内内容: 盒里只有一种东西时列在「商品」下面") {
    auto data         = sampleShop();
    data.itemName     = "潜影盒";
    data.hasProduct   = true;
    data.boxContents  = {{"钻石", 64}};

    auto const lines = shopInfoLines(data);
    CHECK(hasLine(lines, "盒内: 钻石 ×64"));
    // 它在「商品」下面、「每份」上面：买家先看到卖的是什么，再看到盒子里装的是什么。
    auto const itemAt  = std::find_if(lines.begin(), lines.end(), [](Text const& line) {
        return plainText(line).rfind("商品", 0) == 0;
    });
    auto const boxAt   = std::find_if(lines.begin(), lines.end(), [](Text const& line) {
        return plainText(line).rfind("盒内", 0) == 0;
    });
    auto const unitAt  = std::find_if(lines.begin(), lines.end(), [](Text const& line) {
        return plainText(line).rfind("每份", 0) == 0;
    });
    REQUIRE(itemAt != lines.end());
    REQUIRE(boxAt != lines.end());
    REQUIRE(unitAt != lines.end());
    CHECK(itemAt < boxAt);
    CHECK(boxAt < unitAt);
}

TEST_CASE("盒内内容: 不是容器类商品就没有这一行") {
    auto data        = sampleShop();
    data.hasProduct  = true;
    data.boxContents = {};
    for (auto const& line : shopInfoLines(data)) {
        CHECK(plainText(line).rfind("盒内", 0) != 0);
    }
}

TEST_CASE("盒内内容: 盒里有两样以上就不列（正文已经说「盒里装得不整齐」）") {
    auto data        = sampleShop();
    data.hasProduct  = true;
    data.boxContents = {{"钻石", 3}, {"铁锭", 1}};
    for (auto const& line : shopInfoLines(data)) {
        CHECK(plainText(line).rfind("盒内", 0) != 0);
    }
}

TEST_CASE("管理表单: 箱子里有商品才给「更新商品名」") {
    auto data       = sampleShop();
    data.hasProduct = true;
    std::size_t const withProduct = shopManageFormButtons(data).size();
    CHECK(hasButton(shopManageFormButtons(data), "更新商品名"));

    data.hasProduct = false;
    CHECK_FALSE(hasButton(shopManageFormButtons(data), "更新商品名"));
    // 没有商品只是少这一个按钮，改价/改件数/停业照旧在。
    CHECK(shopManageFormButtons(data).size() == withProduct - 1);
}

TEST_CASE("表单标题: 普通店是「<店主> 的商店」，管理员店是「管理员商店」") {
    auto data = sampleShop();
    CHECK(plainText(shopFormTitle(data)) == "Alex 的商店");

    data.adminShop = true;
    CHECK(plainText(shopFormTitle(data)) == "管理员商店");
}

TEST_CASE("表单标题: 店主名缺失时不冒出空洞标题") {
    ShopInfoData data;
    CHECK(plainText(shopFormTitle(data)) == "未知店主的商店");
}

TEST_CASE("表单正文: 一行一项，覆盖店主/商品/份量/买卖价/可成交份数/余额") {
    auto const lines = shopInfoLines(sampleShop());

    CHECK(hasLine(lines, "店主: Alex"));
    CHECK(hasLine(lines, "商品: 钻石"));
    CHECK(hasLine(lines, "每份: 64 个"));
    CHECK(hasLine(lines, "买入价: $ 10 / 份"));
    CHECK(hasLine(lines, "卖出价: $ 5 / 份"));
    CHECK(hasLine(lines, "可成交份数: 3 份"));
}

TEST_CASE("表单正文: 管理员店的店主行不写玩家名") {
    auto data      = sampleShop();
    data.adminShop = true;
    CHECK(hasLine(shopInfoLines(data), "店主: 管理员商店"));
}

TEST_CASE("表单正文: 缺一边价格时写明不出售 / 不收购") {
    auto data = sampleShop();
    data.sellPrice.reset();
    auto const lines = shopInfoLines(data);

    CHECK(hasLine(lines, "买入价: $ 10 / 份"));
    CHECK(hasLine(lines, "卖出价: 不收购"));
}

TEST_CASE("表单正文: 余额读不到时写占位，不编造数字") {
    auto data = sampleShop();
    CHECK_FALSE(data.balance.has_value());

    // 空有两个原因（没有可用后端、读不到这名玩家的账户），正文不猜是哪一个： 
    // 具体原因在不能交易时由「状态： 」那一行说清楚（DESIGN §8.2）。
    auto const lines = shopInfoLines(data);
    CHECK(hasLine(lines, "余额: 暂时读不到"));

    data.balance = 100;
    CHECK(hasLine(shopInfoLines(data), "余额: $ 100"));
}

TEST_CASE("表单正文: 货币符号可配置，空符号不留下多余空格") {
    auto data           = sampleShop();
    data.balance        = 100;
    data.currencySymbol = "金币";
    CHECK(hasLine(shopInfoLines(data), "余额: 金币 100"));

    data.currencySymbol = "";
    CHECK(hasLine(shopInfoLines(data), "余额: 100"));
    CHECK(hasLine(shopInfoLines(data), "买入价: 10 / 份"));
}

TEST_CASE("表单正文: 售罄时明确写 0 份，而不是藏起来") {
    auto data       = sampleShop();
    data.stockUnits = 0;
    CHECK(hasLine(shopInfoLines(data), "可成交份数: 0 份"));
}

TEST_CASE("表单正文: 纯收购店不把可成交份数当卖点") {
    auto data = sampleShop();
    data.buyPrice.reset(); // 只写 S： 玩家只能卖给它
    data.stockUnits = 0;

    auto const lines = shopInfoLines(data);
    CHECK_FALSE(hasLine(lines, "可成交份数: 0 份"));
    CHECK(hasLine(lines, "可成交份数: — (只收购，以你手里的数量为准)"));
}

TEST_CASE("表单正文: 商品名缺失时写占位") {
    auto data = sampleShop();
    data.itemName.clear();
    CHECK(hasLine(shopInfoLines(data), "商品: —"));
}

TEST_CASE("表单正文: 多行拼成一个字符串，行间是换行符") {
    auto const content = plainText(text::block(shopFormContent(sampleShop())));
    CHECK(content.find("店主: Alex") != std::string::npos);
    CHECK(content.find("店主: Alex" + std::string{char(10)} + "商品: 钻石") != std::string::npos);
    // 行尾不留换行，免得表单底部多一行空白
    CHECK(content.back() != char(10));
}

TEST_CASE("买家按钮: 按 DESIGN §10.1 的文案给出购买与出售") {
    auto const buttons = shopFormButtons(sampleShop(), ShopFormRole::Visitor, /*canManage=*/false);

    REQUIRE(buttons.size() == 2);
    CHECK(buttons[0].action == ShopFormAction::Buy);
    // 价格与「一份几件」正文已经列过，按钮只写动作。
    CHECK(plainText(buttons[0].label) == "购买");
    CHECK(buttons[1].action == ShopFormAction::Sell);
    CHECK(plainText(buttons[1].label) == "出售");
}

TEST_CASE("买家按钮: 只有一边价格时只给那一个按钮") {
    auto data = sampleShop();
    data.sellPrice.reset();
    auto const buyOnly = shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false);
    REQUIRE(buyOnly.size() == 1);
    CHECK(buyOnly[0].action == ShopFormAction::Buy);

    data = sampleShop();
    data.buyPrice.reset();
    auto const sellOnly = shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false);
    REQUIRE(sellOnly.size() == 1);
    CHECK(sellOnly[0].action == ShopFormAction::Sell);
}

TEST_CASE("店主按钮: 只有一个管理入口，没有买卖按钮") {
    auto const buttons = shopFormButtons(sampleShop(), ShopFormRole::Manager, /*canManage=*/true);

    REQUIRE(buttons.size() == 1);
    CHECK(buttons[0].action == ShopFormAction::Manage);
    CHECK(plainText(buttons[0].label) == "管理商店");
}

TEST_CASE("管理入口与「现在能不能交易」无关: 停业的店仍给店主 / OP 一个入口") {
    // spec D5： 管理员店的 OP 在交易上是 Visitor，一停业就没按钮的话就永远恢复不了营业。
    auto data              = sampleShop();
    data.adminShop         = true;
    data.unavailableReason = ownerOfflineReason();

    auto const managed = shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/true);
    REQUIRE(managed.size() == 1);
    CHECK(managed[0].action == ShopFormAction::Manage);

    // 没有管理身份的人： 一个按钮都没有（买卖被原因藏起来，管理入口也不该给他）。
    CHECK(shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false).empty());
}

TEST_CASE("管理员店的 OP: 买卖按钮与管理入口同时出现") {
    // 交易上是 Visitor（对手方是系统账户），但它同时是这块牌的管理者。
    auto data      = sampleShop();
    data.adminShop = true;

    auto const buttons = shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/true);
    REQUIRE(buttons.size() == 3);
    CHECK(buttons[0].action == ShopFormAction::Manage);
    CHECK(buttons[1].action == ShopFormAction::Buy);
    CHECK(buttons[2].action == ShopFormAction::Sell);
}

TEST_CASE("商店箱空了: 正文写清原因，不给买卖按钮") {
    auto data = sampleShop();
    data.buyPrice.reset();
    data.sellPrice.reset();
    data.unavailableReason = emptyBoxReason();

    CHECK(plainText(emptyBoxReason()).find("没有货") != std::string::npos);
    CHECK(hasLine(shopInfoLines(data), "状态: " + plainText(emptyBoxReason())));
    CHECK(shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false).empty());
}

TEST_CASE("读不到店主余额的说法: 讲的是余额，不是店主没钱") {
    auto const reason = ownerFundsUnknownReason();
    CHECK(plainText(reason).find("余额") != std::string::npos);
    CHECK(plainText(reason).find("资金不足") == std::string::npos);
    CHECK(plainText(reason) != plainText(ownerOfflineReason()));
    CHECK(plainText(reason) != plainText(noEconomyReason()));
}

TEST_CASE("后端当不了系统账户的说法: 说的是经济后端能力，不是玩家的问题") {
    auto const reason = noSystemAccountReason();
    CHECK_FALSE(text::empty(reason));
    CHECK(plainText(reason).find("系统账户") != std::string::npos);
    CHECK(plainText(reason).find("管理员商店") != std::string::npos);
    // 与「没有后端」「店主不可达」是两回事，文案不能串行。
    CHECK(plainText(reason) != plainText(noEconomyReason()));
    CHECK(plainText(reason) != plainText(ownerOfflineReason()));
}

TEST_CASE("表单角色: 普通店店主与 OP 拿管理入口，管理员店所有人都能买卖") {
    CHECK(shopFormRoleOf(ShopActor::Owner, /*adminShop=*/false) == ShopFormRole::Manager);
    CHECK(shopFormRoleOf(ShopActor::Operator, /*adminShop=*/false) == ShopFormRole::Manager);
    CHECK(shopFormRoleOf(ShopActor::Other, false) == ShopFormRole::Visitor);
    CHECK(shopFormRoleOf(ShopActor::NoRecordedOwner, false) == ShopFormRole::Visitor);

    // 管理员店： 对手方是系统账户（§7.4），店主不是交易的一侧，OP 也能买卖（票 11）
    for (auto const actor : {ShopActor::Owner, ShopActor::Operator, ShopActor::Other, ShopActor::NoRecordedOwner}) {
        CHECK(shopFormRoleOf(actor, /*adminShop=*/true) == ShopFormRole::Visitor);
    }
}

TEST_CASE("管理员店 + 后端当不了系统账户: 正文写原因，买卖按钮一个不给") {
    auto data              = sampleShop();
    data.adminShop         = true;
    data.unavailableReason = noSystemAccountReason();

    CHECK(hasLine(shopInfoLines(data), "状态: " + plainText(noSystemAccountReason())));
    CHECK(shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false).empty());
}

TEST_CASE("表单正文: 不能交易时写明原因，且不给买卖按钮") {
    auto data              = sampleShop();
    data.unavailableReason = text::lit("店主不在线，现在不能交易");

    CHECK(hasLine(shopInfoLines(data), "状态: 店主不在线，现在不能交易"));
    CHECK(shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false).empty());

    data.unavailableReason = text::lit("服务器没有可用的经济后端，暂时不能交易");
    CHECK(shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false).empty());
}

TEST_CASE("表单按钮: 能交易时买家拿到购买与出售两个按钮") {
    auto const buttons = shopFormButtons(sampleShop(), ShopFormRole::Visitor, /*canManage=*/false);

    REQUIRE(buttons.size() == 2);
    CHECK(buttons[0].action == ShopFormAction::Buy);
    CHECK(buttons[1].action == ShopFormAction::Sell);
}

TEST_CASE("停业: 正文多一行「这家店暂停营业」") {
    auto open = sampleShop();
    CHECK_FALSE(hasLine(shopInfoLines(open), "这家店暂停营业"));

    auto closed = open;
    closed.closed = true;
    CHECK(hasLine(shopInfoLines(closed), "这家店暂停营业"));
    // 其余正文照旧： 停业只是多一行状态，价格与库存都留着
    CHECK(hasLine(shopInfoLines(closed), "买入价: $ 10 / 份"));
    CHECK(hasLine(shopInfoLines(closed), "可成交份数: 3 份"));
}

TEST_CASE("停业: 一个买卖按钮都不给") {
    auto data   = sampleShop();
    data.closed = true;

    CHECK(shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false).empty());
    // 店主本来就没有买卖按钮，停业与否都一样
    CHECK(shopFormButtons(data, ShopFormRole::Manager, /*canManage=*/true).size() == 1);
}

TEST_CASE("停业: 管理入口照给 (管理员店的 OP 是 Visitor，入口一消失就永久停业)") {
    // 回归（spec D5 / 票 03）： 停业不许把店主自己锁在外面。
    auto admin      = sampleShop();
    admin.adminShop = true;
    admin.closed    = true;

    auto const adminButtons = shopFormButtons(admin, ShopFormRole::Visitor, /*canManage=*/true);
    REQUIRE(adminButtons.size() == 1);
    CHECK(adminButtons[0].action == ShopFormAction::Manage);

    // 普通店的店主同理（Manager 分支先返回，不碰 closed）
    auto own   = sampleShop();
    own.closed = true;
    auto const ownerButtons = shopFormButtons(own, ShopFormRole::Manager, /*canManage=*/true);
    REQUIRE(ownerButtons.size() == 1);
    CHECK(ownerButtons[0].action == ShopFormAction::Manage);

    // 没有管理身份的人： 停业的店一个按钮都没有
    CHECK(shopFormButtons(own, ShopFormRole::Visitor, /*canManage=*/false).empty());
}

TEST_CASE("表单正文: 无限库存写「无限」，不编一个数字 (DESIGN §7.4 / §10.1)") {
    auto admin       = sampleShop();
    admin.adminShop  = true;
    admin.stockUnits = StockView::makeUnlimited();

    auto const lines = shopInfoLines(admin);
    CHECK(hasLine(lines, "可成交份数: 无限"));
    // 旧的有限说法一个字都不该留下： 同一个正文里两种真相最容易被读成「只有 3 份」
    CHECK_FALSE(hasLine(lines, "可成交份数: 3 份"));
}

TEST_CASE("表单按钮: 无限库存照常给买卖按钮，库存不是藏按钮的理由") {
    auto admin       = sampleShop();
    admin.adminShop  = true;
    admin.stockUnits = StockView::makeUnlimited();

    auto const buttons = shopFormButtons(admin, ShopFormRole::Visitor, /*canManage=*/false);
    REQUIRE(buttons.size() == 2);
    CHECK(buttons[0].action == ShopFormAction::Buy);
    CHECK(buttons[1].action == ShopFormAction::Sell);
}

TEST_CASE("模板箱空: 管理员店说「还没设商品」，与普通店「没有货」是两条不同的原因") {
    auto const adminReason   = unavailableReasonForEmptyBox(/* adminShop=*/true);
    auto const regularReason = unavailableReasonForEmptyBox(/* adminShop=*/false);

    CHECK(plainText(adminReason) != plainText(regularReason));
    CHECK(plainText(adminReason).find("还没设商品") != std::string::npos);
    CHECK(plainText(adminReason).find("样品") != std::string::npos);
    // 普通店的原因一个字都没变： 箱子空了就是缺货，补货就恢复
    CHECK(plainText(regularReason) == plainText(emptyBoxReason()));
    CHECK(plainText(regularReason).find("没有货") != std::string::npos);
}

TEST_CASE("箱子的四种状态: 优先级是 混装 > 空箱 > 只剩样品 > 有货可卖") {
    CHECK(boxStockStateOf(false, false, true) == BoxStockState::Saleable);
    CHECK(boxStockStateOf(false, false, false) == BoxStockState::OnlySampleLeft);
    CHECK(boxStockStateOf(true, false, false) == BoxStockState::Empty);
    // 混装是“商品定义本身”有问题，比缺不缺货更根本：空箱与“有货”都要让位
    CHECK(boxStockStateOf(true, true, false) == BoxStockState::MixedContents);
    CHECK(boxStockStateOf(false, true, true) == BoxStockState::MixedContents);
    CHECK(boxStockStateOf(false, true, false) == BoxStockState::MixedContents);
}

TEST_CASE("箱子的状态: 只有「有货可卖」与「只剩样品」没有拦按钮的原因") {
    CHECK(text::empty(unavailableReasonForStockState(BoxStockState::Saleable, /* adminShop=*/false)));
    // 只剩样品时店收购方向还能用，所以不能拦按钮；它的说明走 `stockNoteForStockState`
    CHECK(text::empty(unavailableReasonForStockState(BoxStockState::OnlySampleLeft, false)));

    auto const mixed = plainText(unavailableReasonForStockState(BoxStockState::MixedContents, false));
    auto const empty = plainText(unavailableReasonForStockState(BoxStockState::Empty, false));
    CHECK(mixed != empty);
    // 空箱仍按店的性质分叉（管理员店是“还没设商品”）
    CHECK(empty == plainText(unavailableReasonForEmptyBox(/* adminShop=*/false)));
    CHECK(
        plainText(unavailableReasonForStockState(BoxStockState::Empty, /* adminShop=*/true))
        == plainText(unavailableReasonForEmptyBox(/* adminShop=*/true))
    );
    // “只剩样品”绝不能说成“没货”：那是叫店主再补一批，不是等补货
    auto const onlySample = plainText(stockNoteForStockState(BoxStockState::OnlySampleLeft));
    CHECK(onlySample.find("样品") != std::string::npos);
    CHECK(onlySample != empty);
    CHECK(mixed.find("整齐") != std::string::npos);
    // 其他状态没有说明句
    CHECK(text::empty(stockNoteForStockState(BoxStockState::Saleable)));
    CHECK(text::empty(stockNoteForStockState(BoxStockState::Empty)));
}

TEST_CASE("只剩样品: 只留「出售」按钮（店收购方向可用），正文明说为什么 0 份") {
    auto data      = sampleShop();
    data.stockUnits = StockView::makeFinite(0);
    data.stockNote  = stockNoteForStockState(BoxStockState::OnlySampleLeft);

    // 可成交份数是 0，“购买”进去只会得到一句“商店的货不够”，所以不给；
    // 而玩家把货卖给店能让可卖件数变多，那个方向必须留着。
    auto const buttons = shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false);
    REQUIRE(buttons.size() == 1);
    CHECK(buttons[0].action == ShopFormAction::Sell);

    auto const lines = shopInfoLines(data);
    CHECK(hasLine(lines, "可成交份数: 0 份"));
    CHECK(hasLine(lines, "状态: " + plainText(data.stockNote)));
}

TEST_CASE("盒内混装: 拦按钮，正文写「装得不整齐」而不是「没有货」") {
    auto data               = sampleShop();
    data.unavailableReason  = unavailableReasonForStockState(BoxStockState::MixedContents, /* adminShop=*/false);

    CHECK(shopFormButtons(data, ShopFormRole::Visitor, /*canManage=*/false).empty());
    auto const lines = shopInfoLines(data);
    CHECK(hasLine(lines, "状态: " + plainText(data.unavailableReason)));
    CHECK_FALSE(hasLine(lines, "状态: " + plainText(emptyBoxReason())));
}

TEST_CASE("模板箱空: 正文写「还没设商品」，不是「商店箱里没有货」") {
    auto admin            = sampleShop();
    admin.adminShop       = true;
    admin.stockUnits      = StockView::makeUnlimited();
    admin.unavailableReason = unavailableReasonForEmptyBox(/* adminShop=*/true);

    auto const lines = shopInfoLines(admin);
    CHECK(hasLine(lines, "状态: " + plainText(unavailableReasonForEmptyBox(/* adminShop=*/true))));
    CHECK_FALSE(hasLine(lines, "状态: " + plainText(emptyBoxReason())));
}

TEST_CASE("模板箱空: 买卖按钮一个都不给，管理入口照给 (OP 才能进去补样品)") {
    auto admin              = sampleShop();
    admin.adminShop         = true;
    admin.stockUnits        = StockView::makeUnlimited();
    admin.unavailableReason = unavailableReasonForEmptyBox(/* adminShop=*/true);

    auto const visitor = shopFormButtons(admin, ShopFormRole::Visitor, /*canManage=*/false);
    CHECK(visitor.empty());

    auto const op = shopFormButtons(admin, ShopFormRole::Visitor, /*canManage=*/true);
    REQUIRE(op.size() == 1);
    CHECK(op[0].action == ShopFormAction::Manage);
}

TEST_CASE("模板箱有样品: 买卖按钮照常给，与「还没设商品」是两种状态") {
    auto admin         = sampleShop();
    admin.adminShop    = true;
    admin.stockUnits   = StockView::makeUnlimited();
    admin.unavailableReason = Text{}; // 有样品 → 没有「不可交易」的原因

    auto const buttons = shopFormButtons(admin, ShopFormRole::Visitor, /*canManage=*/true);
    REQUIRE(buttons.size() == 3); // 管理 + 购买 + 出售
    CHECK(buttons[0].action == ShopFormAction::Manage);
    CHECK(buttons[1].action == ShopFormAction::Buy);
    CHECK(buttons[2].action == ShopFormAction::Sell);
}
