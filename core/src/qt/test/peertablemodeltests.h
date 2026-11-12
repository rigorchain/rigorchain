// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TEST_PEERTABLEMODELTESTS_H
#define BITCOIN_QT_TEST_PEERTABLEMODELTESTS_H

#include <QObject>
#include <QTest>

namespace interfaces {
class Node;
} // namespace interfaces

class PeerTableModelTests : public QObject
{
    Q_OBJECT

public:
    explicit PeerTableModelTests(interfaces::Node& node) : m_node(node) {}

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void normalCallbackWhileAlive();
    void destroyedModelDropsQueuedCallback();
    void destroyingOneModelLeavesTheOther();
    void repeatedCreateDestroyLeavesNoWatcher();
    void unwatchIsIdempotentAndDoesNotCancelAnother();
    void nullClientModelSkipsNetworkUpdate();

private:
    interfaces::Node& m_node;
};

#endif // BITCOIN_QT_TEST_PEERTABLEMODELTESTS_H
