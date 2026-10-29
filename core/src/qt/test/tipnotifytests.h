// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_TIPNOTIFYTESTS_H
#define BITCOIN_QT_TEST_TIPNOTIFYTESTS_H

#include <QObject>
#include <QTest>

class TipNotifyTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void destroyedHostDropsTrailingFlush();
    void liveHostRunsTrailingFlush();
};

#endif // BITCOIN_QT_TEST_TIPNOTIFYTESTS_H
