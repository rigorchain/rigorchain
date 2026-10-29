// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/tipnotifytests.h>
#include <qt/tipnotifyflush.h>

#include <QCoreApplication>

namespace {

void ArmTrailingFlush(QObject* host, int* hits)
{
    ArmContextFlush(host, 0, [hits] { ++*hits; });
}

} // namespace

void TipNotifyTests::destroyedHostDropsTrailingFlush()
{
    int hits = 0;
    auto* host = new QObject;
    ArmTrailingFlush(host, &hits);
    delete host;
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(hits, 0);
}

void TipNotifyTests::liveHostRunsTrailingFlush()
{
    int hits = 0;
    auto* host = new QObject;
    ArmTrailingFlush(host, &hits);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(hits, 1);
    delete host;
    QCoreApplication::processEvents();
    QCOMPARE(hits, 1);
}

#ifdef TIPNOTIFY_LIFETIME_STANDALONE
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TipNotifyTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#endif
