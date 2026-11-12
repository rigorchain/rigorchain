// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/peertablemodeltests.h>

#include <interfaces/node.h>
#include <qt/peertablemodel.h>
#include <qt/platformstyle.h>
#include <qt/rpcconsole.h>

#include <cstdint>
#include <memory>

#include <QLabel>
#include <QTest>

void PeerTableModelTests::initTestCase()
{
    // Keep the lifecycle tests off the public resolver. Production lookups stay enabled
    // after cleanupTestCase, so the later GUI tests still exercise the real callback.
    SetConfiguredSeedLookupsEnabledForTest(false);
}

void PeerTableModelTests::cleanupTestCase()
{
    SetConfiguredSeedLookupsEnabledForTest(true);
}

void PeerTableModelTests::normalCallbackWhileAlive()
{
    const int before = ConfiguredSeedWatcherCountForTest();
    PeerTableModel model(m_node, nullptr);
    QCOMPARE(ConfiguredSeedWatcherCountForTest(), before + 1);
    NotifyConfiguredSeedWatchersForTest();
    QCOMPARE(model.rowCount(), 0);
}

void PeerTableModelTests::destroyedModelDropsQueuedCallback()
{
    const int before = ConfiguredSeedWatcherCountForTest();
    {
        PeerTableModel model(m_node, nullptr);
        QCOMPARE(ConfiguredSeedWatcherCountForTest(), before + 1);
    }
    QCOMPARE(ConfiguredSeedWatcherCountForTest(), before);
    NotifyConfiguredSeedWatchersForTest();
    QCOMPARE(ConfiguredSeedWatcherCountForTest(), before);
}

void PeerTableModelTests::destroyingOneModelLeavesTheOther()
{
    const int before = ConfiguredSeedWatcherCountForTest();
    auto* first = new PeerTableModel(m_node, nullptr);
    auto* second = new PeerTableModel(m_node, nullptr);
    QCOMPARE(ConfiguredSeedWatcherCountForTest(), before + 2);
    delete first;
    QCOMPARE(ConfiguredSeedWatcherCountForTest(), before + 1);
    NotifyConfiguredSeedWatchersForTest();
    QCOMPARE(second->rowCount(), 0);
    second->refresh();
    QCOMPARE(second->rowCount(), 0);
    delete second;
    QCOMPARE(ConfiguredSeedWatcherCountForTest(), before);
    NotifyConfiguredSeedWatchersForTest();
}

void PeerTableModelTests::repeatedCreateDestroyLeavesNoWatcher()
{
    const int before = ConfiguredSeedWatcherCountForTest();
    for (int i = 0; i < 8; ++i) {
        auto* model = new PeerTableModel(m_node, nullptr);
        NotifyConfiguredSeedWatchersForTest();
        delete model;
        NotifyConfiguredSeedWatchersForTest();
        QCOMPARE(ConfiguredSeedWatcherCountForTest(), before);
    }
    auto* keep = new PeerTableModel(m_node, nullptr);
    for (int i = 0; i < 4; ++i) {
        auto* drop = new PeerTableModel(m_node, nullptr);
        NotifyConfiguredSeedWatchersForTest();
        delete drop;
        NotifyConfiguredSeedWatchersForTest();
        QCOMPARE(ConfiguredSeedWatcherCountForTest(), before + 1);
        QCOMPARE(keep->rowCount(), 0);
    }
    delete keep;
    QCOMPARE(ConfiguredSeedWatcherCountForTest(), before);
    NotifyConfiguredSeedWatchersForTest();
}

void PeerTableModelTests::unwatchIsIdempotentAndDoesNotCancelAnother()
{
    int hits_a = 0;
    int hits_b = 0;
    const std::uint64_t id_a = WatchConfiguredSeeds([&hits_a] { ++hits_a; });
    const std::uint64_t id_b = WatchConfiguredSeeds([&hits_b] { ++hits_b; });
    QVERIFY(id_a != 0);
    QVERIFY(id_b != 0);
    QVERIFY(id_a != id_b);
    UnwatchConfiguredSeeds(id_a);
    UnwatchConfiguredSeeds(id_a);
    NotifyConfiguredSeedWatchersForTest();
    QCOMPARE(hits_a, 0);
    QCOMPARE(hits_b, 1);
    UnwatchConfiguredSeeds(id_b);
    UnwatchConfiguredSeeds(id_b);
    NotifyConfiguredSeedWatchersForTest();
    QCOMPARE(hits_a, 0);
    QCOMPARE(hits_b, 1);
}

void PeerTableModelTests::nullClientModelSkipsNetworkUpdate()
{
    std::unique_ptr<const PlatformStyle> platform_style(PlatformStyle::instantiate("other"));
    QVERIFY(platform_style);
    RPCConsole console(m_node, platform_style.get(), nullptr);
    QLabel* connections = console.findChild<QLabel*>("numberOfConnections");
    QVERIFY(connections != nullptr);
    const QString before = connections->text();

    // clientModel is still null. These are the slots a queued
    // networkActiveChanged / numConnectionsChanged signal can invoke
    // after setClientModel(nullptr).
    console.setNetworkActive(false);
    console.setNumConnections(0);
    QCOMPARE(connections->text(), before);

    console.setClientModel(nullptr, 0, 0, 0.0);
    console.setNetworkActive(true);
    QCOMPARE(connections->text(), before);
}
