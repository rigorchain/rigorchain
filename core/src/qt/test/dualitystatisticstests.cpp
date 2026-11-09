// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/dualitystatisticstests.h>

#include <consensus/amount.h>
#include <consensus/rigor_consensus_lock.h>
#include <index/economics_snapshot.h>
#include <qt/bitcoinunits.h>
#include <qt/dualitystatisticspage.h>

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QTranslator>

namespace {

QLabel* label(const DualityStatisticsPage& page, const char* name)
{
    return page.findChild<QLabel*>(QString::fromLatin1(name));
}

EconomicsSnapshot authoritativeSample()
{
    EconomicsSnapshot snapshot;
    snapshot.status = EconomicsStatus::Ok;
    snapshot.amounts_authoritative = true;
    snapshot.generated_at = 1760000000;
    snapshot.indexed_height = 100;
    snapshot.active_height = 100;
    snapshot.lag_blocks = 0;
    snapshot.amounts.subsidy_available = true;
    snapshot.amounts.subsidy = 128 * COIN;
    snapshot.amounts.convergence_available = true;
    snapshot.amounts.convergence = COIN + COIN / 4;
    snapshot.amounts.fee_burn_available = false;
    snapshot.amounts.fee_burn = 0;
    snapshot.amounts.oru_utxo_available = false;
    snapshot.amounts.oru_utxo = 0;
    snapshot.amounts.res_generated_available = true;
    snapshot.amounts.res_generated = 5 * COIN;
    snapshot.amounts.res_supply_available = true;
    snapshot.amounts.res_supply = 2 * COIN;
    snapshot.amounts.res_cap = Consensus::RES_MAX_SUPPLY;
    snapshot.amounts.res_remaining_available = true;
    snapshot.amounts.res_remaining = Consensus::RES_MAX_SUPPLY - snapshot.amounts.res_generated;
    return snapshot;
}

} // namespace

void DualityStatisticsTests::pageIsSeparateAndTimerIsUnique()
{
    DualityStatisticsPage page;
    QCOMPARE(page.objectName(), QStringLiteral("DualityStatisticsPage"));
    QLabel* title = label(page, "dualityTitle");
    QVERIFY(title != nullptr);
    QCOMPARE(title->text(), QStringLiteral("Duality Statistics"));
    QCOMPARE(label(page, "oruModuleTitle")->text(), QStringLiteral("ORU Statistics"));
    QCOMPARE(label(page, "resModuleTitle")->text(), QStringLiteral("RES Statistics"));
    QCOMPARE(label(page, "statusModuleTitle")->text(), QStringLiteral("Index & Data Status"));
    QCOMPARE(label(page, "dualityInterval")->text(), QStringLiteral("300 seconds"));
    const QList<QTimer*> timers = page.findChildren<QTimer*>(QStringLiteral("dualityRefreshTimer"));
    QCOMPARE(timers.size(), 1);
    QCOMPARE(timers.front()->interval(), 300000);
    QVERIFY(!timers.front()->isActive());
    QCOMPARE(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(ECONOMICS_REFRESH_INTERVAL).count()), 300000);

    page.show();
    QVERIFY(QTest::qWaitFor([&] { return timers.front()->isActive(); }, 1000));
    page.hide();
    QVERIFY(!timers.front()->isActive());
    page.show();
    QCOMPARE(page.findChildren<QTimer*>(QStringLiteral("dualityRefreshTimer")).size(), 1);
    page.hide();
}

void DualityStatisticsTests::disabledIndexDoesNotShowZero()
{
    DualityStatisticsPage page;
    page.show();
    QLabel* remaining = label(page, "resRemainingValue");
    QLabel* supply = label(page, "resSupplyValue");
    QLabel* schedule = label(page, "oruScheduleValue");
    QLabel* fee = label(page, "oruFeeValue");
    QLabel* utxo = label(page, "oruUtxoValue");
    QLabel* convergence = label(page, "oruConvergenceValue");
    QVERIFY(remaining != nullptr);
    QVERIFY(supply != nullptr);
    QVERIFY(schedule != nullptr);
    QVERIFY(fee != nullptr);
    QVERIFY(utxo != nullptr);
    QVERIFY(convergence != nullptr);
    QVERIFY(QTest::qWaitFor([&] {
        return remaining->text() == QStringLiteral("N/A") && supply->text() == QStringLiteral("N/A");
    }, 2000));
    QCOMPARE(schedule->text(), QStringLiteral("N/A"));
    QCOMPARE(fee->text(), QStringLiteral("N/A"));
    QCOMPARE(utxo->text(), QStringLiteral("N/A"));
    QCOMPARE(convergence->text(), QStringLiteral("N/A"));
    QVERIFY(!schedule->text().contains(QStringLiteral("0.00000000")));
    QVERIFY(!supply->text().contains(QStringLiteral("0.00000000")));
    QVERIFY(supply->objectName() != remaining->objectName());
    page.hide();
}

void DualityStatisticsTests::snapshotValuesAndStaleRetention()
{
    DualityStatisticsPage page;
    const EconomicsSnapshot current = authoritativeSample();
    page.applySnapshot(current);

    const QString schedule = BitcoinUnits::formatAmountForAsset(ASSET_ORU, current.amounts.subsidy, BitcoinUnits::Unit::ORU);
    const QString burned = BitcoinUnits::formatAmountForAsset(ASSET_ORU, current.amounts.convergence, BitcoinUnits::Unit::ORU);
    const QString remaining = BitcoinUnits::formatAmountForAsset(ASSET_RES, current.amounts.res_remaining, BitcoinUnits::Unit::ORU);
    const QString supply = BitcoinUnits::formatAmountForAsset(ASSET_RES, current.amounts.res_supply, BitcoinUnits::Unit::ORU);
    QCOMPARE(label(page, "oruScheduleValue")->text(), schedule);
    QCOMPARE(label(page, "oruConvergenceValue")->text(), burned);
    QCOMPARE(label(page, "oruFeeValue")->text(), QStringLiteral("N/A"));
    QCOMPARE(label(page, "oruUtxoValue")->text(), QStringLiteral("N/A"));
    QCOMPARE(label(page, "resSupplyValue")->text(), supply);
    QCOMPARE(label(page, "resRemainingValue")->text(), remaining);
    QCOMPARE(label(page, "resSupplyTitle")->text(), QStringLiteral("Current RES UTXO Supply"));
    QCOMPARE(label(page, "resRemainingTitle")->text(), QStringLiteral("Remaining RES Issuance Capacity"));
    QVERIFY(remaining != supply);
    QVERIFY(!label(page, "oruFeeValue")->text().contains(QStringLiteral("0")));
    QCOMPARE(label(page, "dualityIndexState")->text(), QStringLiteral("Synced"));
    QCOMPARE(label(page, "dualityFreshness")->text(), QStringLiteral("Current"));
    QVERIFY(label(page, "dualityRefreshed")->text().startsWith(QStringLiteral("Last successful update:")));

    EconomicsSnapshot lagging = current;
    lagging.status = EconomicsStatus::Lagging;
    lagging.lag_blocks = 3;
    lagging.active_height = 103;
    page.applySnapshot(lagging);
    QCOMPARE(label(page, "oruScheduleValue")->text(), schedule);
    QCOMPARE(label(page, "dualitySyncState")->text(), QStringLiteral("Lag: 3 blocks"));
    QCOMPARE(label(page, "resSupplyValue")->text(), supply);

    EconomicsSnapshot failed = current;
    failed.status = EconomicsStatus::RefreshFailed;
    failed.refresh_failed = true;
    page.applySnapshot(failed);
    QCOMPARE(label(page, "oruScheduleValue")->text(), schedule);
    QCOMPARE(label(page, "oruConvergenceValue")->text(), burned);
    QCOMPARE(label(page, "resRemainingValue")->text(), remaining);
    QCOMPARE(label(page, "resSupplyValue")->text(), supply);
    QCOMPARE(label(page, "oruFeeValue")->text(), QStringLiteral("N/A"));
    QCOMPARE(label(page, "dualityFreshness")->text(), QStringLiteral("Stale Data"));
    QCOMPARE(label(page, "dualityIndexState")->text(), QStringLiteral("Stale Data"));
    QVERIFY(label(page, "dualityStatus")->text().contains(QStringLiteral("Stale Data")));
    QVERIFY(label(page, "dualityStatus")->text().contains(QStringLiteral("300 seconds")));
    QVERIFY(label(page, "dualityRefreshed")->text().startsWith(QStringLiteral("Last successful update:")));

    EconomicsSnapshot busy = current;
    busy.status = EconomicsStatus::Busy;
    page.applySnapshot(busy);
    QCOMPARE(label(page, "oruScheduleValue")->text(), schedule);
    QCOMPARE(label(page, "dualityFreshness")->text(), QStringLiteral("Refreshing"));
    QCOMPARE(label(page, "dualityIndexState")->text(), QStringLiteral("Synced"));

    EconomicsSnapshot off_chain;
    off_chain.status = EconomicsStatus::Stale;
    page.applySnapshot(off_chain);
    QCOMPARE(label(page, "oruScheduleValue")->text(), QStringLiteral("N/A"));
    QCOMPARE(label(page, "resRemainingValue")->text(), QStringLiteral("N/A"));
    QCOMPARE(label(page, "resSupplyValue")->text(), QStringLiteral("N/A"));
    QCOMPARE(label(page, "dualityFreshness")->text(), QStringLiteral("Stale Data"));
    QVERIFY(!label(page, "oruScheduleValue")->text().contains(QStringLiteral("0.00000000")));

    EconomicsSnapshot zero_subsidy = current;
    zero_subsidy.amounts.subsidy = 0;
    zero_subsidy.amounts.subsidy_available = true;
    page.applySnapshot(zero_subsidy);
    QVERIFY(label(page, "oruScheduleValue")->text().contains(QStringLiteral("0.00000000")));
    QCOMPARE(label(page, "oruFeeValue")->text(), QStringLiteral("N/A"));
}

void DualityStatisticsTests::layoutReflowsWithWindowWidth()
{
    DualityStatisticsPage page;
    auto card = [&](const char* name) { return page.findChild<QWidget*>(QString::fromLatin1(name)); };
    auto origin = [&](const char* name) { return card(name)->mapTo(&page, QPoint(0, 0)); };
    auto card_x = [&](const char* name) { return origin(name).x(); };
    auto card_y = [&](const char* name) { return origin(name).y(); };
    page.resize(1100, 800);
    page.show();
    QVERIFY(QTest::qWaitFor([&] { return card_x("oruCard1") > card_x("oruCard0") + 20 && card_x("oruCard3") > card_x("oruCard2") + 20; }, 1000));
    QVERIFY(card_x("resCard1") > card_x("resCard0") + 20);
    QVERIFY(qAbs(card_y("oruCard0") - card_y("oruCard1")) < 8);

    page.resize(720, 900);
    QVERIFY(QTest::qWaitFor([&] {
        return card_x("oruCard1") > card_x("oruCard0") + 20 && card_y("oruCard2") > card_y("oruCard0") + 20;
    }, 1000));
    QVERIFY(card_x("resCard1") > card_x("resCard0") + 20);

    page.resize(420, 1100);
    QVERIFY(QTest::qWaitFor([&] {
        return qAbs(card_x("oruCard1") - card_x("oruCard0")) < 8 && card_y("oruCard1") > card_y("oruCard0") + 20 &&
               qAbs(card_x("resCard1") - card_x("resCard0")) < 8 && card_y("resCard1") > card_y("resCard0") + 20;
    }, 1000));
    QWidget* status = page.findChild<QWidget*>(QStringLiteral("dualityStatusPanel"));
    QVERIFY(status != nullptr);
    QVERIFY(status->mapTo(&page, QPoint(0, 0)).y() > card_y("resCard1"));
    page.hide();
}

void DualityStatisticsTests::wideCardsKeepAmountBaselinesLevel()
{
    DualityStatisticsPage page;
    page.resize(3200, 900);
    page.show();

    EconomicsSnapshot snapshot = authoritativeSample();
    snapshot.amounts.subsidy = 388480 * COIN;
    snapshot.amounts.subsidy_available = true;
    snapshot.amounts.fee_burn = 1210;
    snapshot.amounts.fee_burn_available = true;
    snapshot.amounts.res_supply = 35 * COIN + 92474757;
    snapshot.amounts.res_supply_available = true;
    page.applySnapshot(snapshot);

    QLabel* grouped = label(page, "oruScheduleValue");
    QLabel* plain = label(page, "oruFeeValue");
    QLabel* res_plain = label(page, "resSupplyValue");
    QLabel* res_grouped = label(page, "resRemainingValue");
    QVERIFY(grouped->text().contains(QChar(THIN_SP_CP)));
    QVERIFY(!plain->text().contains(QChar(THIN_SP_CP)));
    QVERIFY(plain->text().startsWith(QStringLiteral("0.00001210")));
    QVERIFY(!res_plain->text().contains(QChar(THIN_SP_CP)));
    QVERIFY(res_grouped->text().contains(QChar(THIN_SP_CP)));
    for (QLabel* amount : {grouped, plain, res_plain, res_grouped}) {
        QVERIFY(!amount->wordWrap());
        QCOMPARE(amount->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    }

    auto top = [&](QLabel* amount) { return amount->mapTo(&page, QPoint(0, 0)).y(); };
    QVERIFY(QTest::qWaitFor([&] {
        return top(grouped) > 0 && grouped->height() > 12 && qAbs(top(grouped) - top(plain)) < 2 &&
               qAbs(top(res_plain) - top(res_grouped)) < 2 && grouped->height() == plain->height() &&
               res_plain->height() == res_grouped->height();
    }, 1000));
    page.hide();
}

void DualityStatisticsTests::iconIsTransparentOverviewStyle()
{
    const QImage image(QStringLiteral(":/icons/duality_statistics"));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(128, 128));
    QVERIFY(image.hasAlphaChannel());
    const QPoint corners[4] = {QPoint(0, 0), QPoint(127, 0), QPoint(0, 127), QPoint(127, 127)};
    for (const QPoint& corner : corners) {
        QCOMPARE(qAlpha(image.pixel(corner)), 0);
    }
    int black = 0;
    int other = 0;
    int clear = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            const int alpha = qAlpha(pixel);
            if (alpha == 0) {
                ++clear;
                continue;
            }
            if (alpha > 200 && qRed(pixel) < 16 && qGreen(pixel) < 16 && qBlue(pixel) < 16) {
                ++black;
            } else if (alpha > 200) {
                ++other;
            }
        }
    }
    QVERIFY(black > 500);
    QCOMPARE(other, 0);
    QVERIFY(clear > 128 * 128 / 2);
    QVERIFY(!QImage(QStringLiteral(":/icons/overview")).isNull());
    QVERIFY(!QImage(QStringLiteral(":/icons/tx_inout")).isNull());
    QVERIFY(!QImage(QStringLiteral(":/icons/convergence")).isNull());

    QImage colored = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < colored.height(); ++y) {
        for (int x = 0; x < colored.width(); ++x) {
            const QRgb pixel = colored.pixel(x, y);
            colored.setPixel(x, y, qRgba(255, 255, 255, qAlpha(pixel)));
        }
    }
    int white = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) <= 200) continue;
            const QRgb painted = colored.pixel(x, y);
            QCOMPARE(qRed(painted), 255);
            QCOMPARE(qGreen(painted), 255);
            QCOMPARE(qBlue(painted), 255);
            QCOMPARE(qAlpha(painted), qAlpha(image.pixel(x, y)));
            ++white;
        }
    }
    QCOMPARE(white, black);

    QToolBar bar;
    bar.setStyleSheet(QStringLiteral("QToolBar { background: #000000; } QToolButton { color: #000000; background: #000000; }"));
    bar.setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar.setIconSize(QSize(32, 32));
    bar.addAction(QIcon(QPixmap::fromImage(colored)), QString());
    bar.resize(220, 56);
    bar.show();
    QVERIFY(QTest::qWaitFor([&] { return !bar.grab().isNull(); }, 1000));
    const QImage shot = bar.grab().toImage();
    int toolbar_white = 0;
    for (int y = 0; y < shot.height(); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            const QRgb pixel = shot.pixel(x, y);
            if (qRed(pixel) > 240 && qGreen(pixel) > 240 && qBlue(pixel) > 240) ++toolbar_white;
        }
    }
    QVERIFY2(toolbar_white > 20, "colorized icon did not paint white pixels on the toolbar");
}

void DualityStatisticsTests::chineseCatalogsTranslateTheNewLabels()
{
    const char* simplified_langs[] = {"zh_CN", "zh", "zh-Hans"};
    const char* traditional_langs[] = {"zh_TW", "zh_HK", "zh-Hant"};
    const char* page_sources[] = {
        "Duality Statistics",
        "Block Rewards Issued",
        "Current RES UTXO Supply",
        "Remaining RES Issuance Capacity",
        "Unavailable",
        "N/A",
        "Stale Data",
    };
    for (const char* lang : simplified_langs) {
        QTranslator translator;
        QVERIFY2(translator.load(QString::fromLatin1(lang), QStringLiteral(":/translations/")), lang);
        QCOMPARE(translator.translate("RigorGUI", "&Duality Statistics"), QStringLiteral("双资产统计(&D)"));
        const QString tip = translator.translate("RigorGUI", "Duality Statistics: local ORU and RES figures from this node");
        QVERIFY2(tip.isEmpty() || tip == QStringLiteral("Duality Statistics: local ORU and RES figures from this node"), lang);
        for (const char* source : page_sources) {
            QCOMPARE(translator.translate("DualityStatisticsPage", source), QString());
        }
        QApplication::instance()->installTranslator(&translator);
        DualityStatisticsPage page;
        QCOMPARE(label(page, "dualityTitle")->text(), QStringLiteral("Duality Statistics"));
        QCOMPARE(label(page, "oruModuleTitle")->text(), QStringLiteral("ORU Statistics"));
        QCOMPARE(label(page, "resModuleTitle")->text(), QStringLiteral("RES Statistics"));
        QCOMPARE(label(page, "statusModuleTitle")->text(), QStringLiteral("Index & Data Status"));
        QCOMPARE(label(page, "oruScheduleTitle")->text(), QStringLiteral("Block Rewards Issued"));
        QCOMPARE(label(page, "oruConvergenceTitle")->text(), QStringLiteral("Convergence Burned"));
        QCOMPARE(label(page, "oruFeeTitle")->text(), QStringLiteral("Transaction Fee Burned"));
        QCOMPARE(label(page, "oruUtxoTitle")->text(), QStringLiteral("Current ORU UTXO Supply"));
        QCOMPARE(label(page, "resSupplyTitle")->text(), QStringLiteral("Current RES UTXO Supply"));
        QCOMPARE(label(page, "resRemainingTitle")->text(), QStringLiteral("Remaining RES Issuance Capacity"));
        QCOMPARE(label(page, "oruScheduleValue")->text(), QStringLiteral("N/A"));
        QVERIFY(label(page, "dualityNotes")->text().contains(QStringLiteral("Remaining RES Issuance Capacity is the consensus RES maximum supply minus total RES minted.")));
        QApplication::instance()->removeTranslator(&translator);
    }
    for (const char* lang : traditional_langs) {
        QTranslator translator;
        QVERIFY2(translator.load(QString::fromLatin1(lang), QStringLiteral(":/translations/")), lang);
        QCOMPARE(translator.translate("RigorGUI", "&Duality Statistics"), QStringLiteral("雙資產統計(&D)"));
        const QString tip = translator.translate("RigorGUI", "Duality Statistics: local ORU and RES figures from this node");
        QVERIFY2(tip.isEmpty() || tip == QStringLiteral("Duality Statistics: local ORU and RES figures from this node"), lang);
        for (const char* source : page_sources) {
            QCOMPARE(translator.translate("DualityStatisticsPage", source), QString());
        }
        QApplication::instance()->installTranslator(&translator);
        DualityStatisticsPage page;
        QCOMPARE(label(page, "dualityTitle")->text(), QStringLiteral("Duality Statistics"));
        QCOMPARE(label(page, "resSupplyTitle")->text(), QStringLiteral("Current RES UTXO Supply"));
        QCOMPARE(label(page, "oruScheduleValue")->text(), QStringLiteral("N/A"));
        QApplication::instance()->removeTranslator(&translator);
    }
}
