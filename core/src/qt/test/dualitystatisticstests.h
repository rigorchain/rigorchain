// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_DUALITYSTATISTICSTESTS_H
#define BITCOIN_QT_TEST_DUALITYSTATISTICSTESTS_H

#include <QObject>

class DualityStatisticsTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void pageIsSeparateAndTimerIsUnique();
    void disabledIndexDoesNotShowZero();
    void snapshotValuesAndStaleRetention();
    void layoutReflowsWithWindowWidth();
    void wideCardsKeepAmountBaselinesLevel();
    void iconIsTransparentOverviewStyle();
    void chineseCatalogsTranslateTheNewLabels();
};

#endif // BITCOIN_QT_TEST_DUALITYSTATISTICSTESTS_H
