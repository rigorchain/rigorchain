// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PEERTABLEMODEL_H
#define BITCOIN_QT_PEERTABLEMODEL_H

#include <net_processing.h> // For CNodeStateStats
#include <net.h>

#include <qt/seedlabel.h>

#include <QAbstractTableModel>
#include <QList>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <cstdint>
#include <functional>
#include <vector>

class PeerTablePriv;

namespace interfaces {
class Node;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

struct CNodeCombinedStats {
    CNodeStats nodeStats;
    CNodeStateStats nodeStateStats;
    bool fNodeStateStatsAvailable;
};
Q_DECLARE_METATYPE(CNodeCombinedStats*)

/**
   Qt model providing information about connected peers, similar to the
   "getpeerinfo" RPC call. Used by the rpc console UI.
 */
class PeerTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit PeerTableModel(interfaces::Node& node, QObject* parent);
    ~PeerTableModel();
    void startAutoRefresh();
    void stopAutoRefresh();

    enum ColumnIndex {
        NetNodeId = 0,
        Age,
        Address,
        Seed,
        Direction,
        ConnectionType,
        Network,
        Ping,
        Sent,
        Received,
        Subversion
    };

    enum {
        StatsRole = Qt::UserRole,
    };

    /** @name Methods overridden from QAbstractTableModel
        @{*/
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /*@}*/

public Q_SLOTS:
    void refresh();

private:
    //! Internal peer data structure.
    QList<CNodeCombinedStats> m_peers_data{};
    interfaces::Node& m_node;
    const QStringList columns{
        /*: Title of Peers Table column which contains a
            unique number used to identify a connection. */
        tr("Peer"),
        /*: Title of Peers Table column which indicates the duration (length of time)
            since the peer connection started. */
        tr("Age"),
        /*: Title of Peers Table column which contains the
            IP/Onion/I2P address of the connected peer. */
        tr("Address"),
        /*: Title of Peers Table column which names a configured DNS seed
            when the peer endpoint is that seed's current address. */
        tr("Seed"),
        /*: Title of Peers Table column which indicates the direction
            the peer connection was initiated from. */
        tr("Direction"),
        /*: Title of Peers Table column which describes the type of
            peer connection. The "type" describes why the connection exists. */
        tr("Type"),
        /*: Title of Peers Table column which states the network the peer
            connected through. */
        tr("Network"),
        /*: Title of Peers Table column which indicates the current latency
            of the connection with the peer. */
        tr("Ping"),
        /*: Title of Peers Table column which indicates the total amount of
            network information we have sent to the peer. */
        tr("Sent"),
        /*: Title of Peers Table column which indicates the total amount of
            network information we have received from the peer. */
        tr("Received"),
        /*: Title of Peers Table column which contains the peer's
            User Agent string. */
        tr("User Agent")};
    QTimer* timer{nullptr};
    //! Id of this model's entry in the process-wide seed watcher list. 0 means none.
    std::uint64_t m_seed_watcher_id{0};
};

/** Register on_change and, unless a test has disabled it, start the shared seed lookups once.
 *  Returns an id for UnwatchConfiguredSeeds. Returns 0 when on_change is empty.
 *  The QHostInfo lookups are process-wide. They are not owned by the caller, and
 *  unregistering a watcher does not abort them. */
std::uint64_t WatchConfiguredSeeds(const std::function<void()>& on_change);
/** Remove one watcher. A second call with the same id does nothing. Id 0 does nothing. */
void UnwatchConfiguredSeeds(std::uint64_t watcher_id);
std::vector<ConfiguredSeedLookup> ConfiguredSeedSnapshot();
QString UniqueSeedLabelForIp(const QString& ip);

/** Test seam. When false, watchers are still registered but no QHostInfo lookup is started.
 *  Production leaves this true. */
void SetConfiguredSeedLookupsEnabledForTest(bool enabled);
/** Deliver the same watcher notification a finished lookup would deliver. Does not start DNS. */
void NotifyConfiguredSeedWatchersForTest();
int ConfiguredSeedWatcherCountForTest();

#endif // BITCOIN_QT_PEERTABLEMODEL_H
