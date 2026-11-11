// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/peertablemodel.h>

#include <qt/guiconstants.h>
#include <qt/guiutil.h>

#include <interfaces/node.h>
#include <chainparams.h>

#include <algorithm>
#include <cstdint>
#include <utility>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QList>
#include <QPointer>
#include <QTimer>

struct SeedCacheRow {
    QString name;
    bool finished{false};
    QStringList addresses;
};
struct SeedWatcher {
    std::uint64_t id{0};
    std::function<void()> callback;
};

static QList<SeedCacheRow> g_seed_rows;
static std::vector<SeedWatcher> g_seed_watchers;
static std::uint64_t g_next_seed_watcher_id{1};
static bool g_seed_lookups_started = false;
static bool g_seed_lookups_enabled = true;

static std::vector<ConfiguredSeedLookup> SeedSnapshot()
{
    std::vector<ConfiguredSeedLookup> out;
    out.reserve(static_cast<size_t>(g_seed_rows.size()));
    for (const SeedCacheRow& row : g_seed_rows) {
        ConfiguredSeedLookup item;
        item.name = row.name.toStdString();
        item.lookup_finished = row.finished;
        item.addresses.reserve(static_cast<size_t>(row.addresses.size()));
        for (const QString& address : row.addresses) {
            item.addresses.push_back(address.toStdString());
        }
        out.push_back(std::move(item));
    }
    return out;
}

static void NotifySeedWatchers()
{
    // Copy ids first. A callback may unregister another watcher, including
    // itself, before this loop reaches it. Look up the live callback so an
    // id removed by ~PeerTableModel is not invoked.
    std::vector<std::uint64_t> ids;
    ids.reserve(g_seed_watchers.size());
    for (const SeedWatcher& watcher : g_seed_watchers) {
        ids.push_back(watcher.id);
    }
    for (const std::uint64_t id : ids) {
        std::function<void()> callback;
        for (const SeedWatcher& watcher : g_seed_watchers) {
            if (watcher.id == id) {
                callback = watcher.callback;
                break;
            }
        }
        if (callback) callback();
    }
}

std::uint64_t WatchConfiguredSeeds(const std::function<void()>& on_change)
{
    if (!on_change) return 0;
    const std::uint64_t id = g_next_seed_watcher_id++;
    if (g_next_seed_watcher_id == 0) ++g_next_seed_watcher_id;
    g_seed_watchers.push_back(SeedWatcher{id, on_change});
    if (!g_seed_lookups_enabled) return id;
    if (g_seed_lookups_started || !qApp) {
        for (const SeedCacheRow& row : g_seed_rows) {
            if (row.finished) {
                on_change();
                break;
            }
        }
        return id;
    }
    g_seed_lookups_started = true;
    for (const std::string& raw : Params().DNSSeeds()) {
        QString name = QString::fromStdString(raw);
        if (name.endsWith(QLatin1Char('.'))) name.chop(1);
        if (name.isEmpty()) continue;
        g_seed_rows.append(SeedCacheRow{name, false, {}});
        const int index = g_seed_rows.size() - 1;
        QHostInfo::lookupHost(name, qApp, [index, name](const QHostInfo& info) {
            if (index < 0 || index >= g_seed_rows.size() || g_seed_rows.at(index).name != name) return;
            QStringList addresses;
            if (info.error() == QHostInfo::NoError) {
                for (const QHostAddress& address : info.addresses()) {
                    if (address.protocol() == QAbstractSocket::IPv4Protocol ||
                        address.protocol() == QAbstractSocket::IPv6Protocol) {
                        addresses << address.toString();
                    }
                }
            }
            g_seed_rows[index].addresses = addresses;
            g_seed_rows[index].finished = true;
            NotifySeedWatchers();
        });
    }
    return id;
}

void UnwatchConfiguredSeeds(std::uint64_t watcher_id)
{
    if (watcher_id == 0) return;
    const auto remove = std::remove_if(g_seed_watchers.begin(), g_seed_watchers.end(),
                                        [watcher_id](const SeedWatcher& watcher) { return watcher.id == watcher_id; });
    g_seed_watchers.erase(remove, g_seed_watchers.end());
}

void SetConfiguredSeedLookupsEnabledForTest(bool enabled)
{
    g_seed_lookups_enabled = enabled;
}

void NotifyConfiguredSeedWatchersForTest()
{
    NotifySeedWatchers();
}

int ConfiguredSeedWatcherCountForTest()
{
    return static_cast<int>(g_seed_watchers.size());
}

std::vector<ConfiguredSeedLookup> ConfiguredSeedSnapshot()
{
    return SeedSnapshot();
}

QString UniqueSeedLabelForIp(const QString& ip)
{
    const SeedNameResult result = UniqueConfiguredSeedName(ip.toStdString(), SeedSnapshot());
    if (result.match != SeedNameMatch::Unique) return {};
    return QString::fromStdString(result.name);
}

PeerTableModel::PeerTableModel(interfaces::Node& node, QObject* parent)
    : QAbstractTableModel(parent),
      m_node(node)
{
    // set up timer for auto refresh
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PeerTableModel::refresh);
    timer->setInterval(MODEL_UPDATE_DELAY);

    // load initial data
    refresh();
    // QPointer becomes null once QObject destruction finishes. Combined with
    // UnwatchConfiguredSeeds, a callback already copied by QHostInfo cannot
    // call into this model after it is gone. The lookup itself stays running:
    // it is shared by every model and uses qApp as its Qt receiver.
    QPointer<PeerTableModel> self(this);
    m_seed_watcher_id = WatchConfiguredSeeds([self] {
        if (!self) return;
        if (self->rowCount() <= 0) return;
        const QModelIndex top_left = self->index(0, PeerTableModel::Seed);
        const QModelIndex bottom_right = self->index(self->rowCount() - 1, PeerTableModel::Seed);
        Q_EMIT self->dataChanged(top_left, bottom_right);
    });
}

PeerTableModel::~PeerTableModel()
{
    UnwatchConfiguredSeeds(m_seed_watcher_id);
    m_seed_watcher_id = 0;
}

void PeerTableModel::startAutoRefresh()
{
    timer->start();
}

void PeerTableModel::stopAutoRefresh()
{
    timer->stop();
}

int PeerTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_peers_data.size();
}

int PeerTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return columns.length();
}

QVariant PeerTableModel::data(const QModelIndex& index, int role) const
{
    if(!index.isValid())
        return QVariant();

    CNodeCombinedStats *rec = static_cast<CNodeCombinedStats*>(index.internalPointer());

    const auto column = static_cast<ColumnIndex>(index.column());
    if (role == Qt::DisplayRole) {
        switch (column) {
        case NetNodeId:
            return (qint64)rec->nodeStats.nodeid;
        case Age:
            return GUIUtil::FormatPeerAge(rec->nodeStats.m_connected);
        case Address:
            return QString::fromStdString(rec->nodeStats.addr.ToStringAddrPort());
        case Seed:
            return UniqueSeedLabelForIp(QString::fromStdString(rec->nodeStats.addr.ToStringAddr()));
        case Direction:
            return QString(rec->nodeStats.fInbound ?
                               //: An Inbound Connection from a Peer.
                               tr("Inbound") :
                               //: An Outbound Connection to a Peer.
                               tr("Outbound"));
        case ConnectionType:
            return GUIUtil::ConnectionTypeToQString(rec->nodeStats.m_conn_type, /*prepend_direction=*/false);
        case Network:
            return GUIUtil::NetworkToQString(rec->nodeStats.m_network);
        case Ping:
            return GUIUtil::formatPingTime(rec->nodeStats.m_min_ping_time);
        case Sent:
            return GUIUtil::formatBytes(rec->nodeStats.nSendBytes);
        case Received:
            return GUIUtil::formatBytes(rec->nodeStats.nRecvBytes);
        case Subversion:
            return QString::fromStdString(rec->nodeStats.cleanSubVer);
        } // no default case, so the compiler can warn about missing cases
        assert(false);
    } else if (role == Qt::TextAlignmentRole) {
        switch (column) {
        case NetNodeId:
        case Age:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case Address:
        case Seed:
            return {};
        case Direction:
        case ConnectionType:
        case Network:
            return QVariant(Qt::AlignCenter);
        case Ping:
        case Sent:
        case Received:
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
        case Subversion:
            return {};
        } // no default case, so the compiler can warn about missing cases
        assert(false);
    } else if (role == StatsRole) {
        return QVariant::fromValue(rec);
    }

    return QVariant();
}

QVariant PeerTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole && section < columns.size())
        {
            return columns[section];
        }
    }
    return QVariant();
}

Qt::ItemFlags PeerTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;

    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    return retval;
}

QModelIndex PeerTableModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);

    if (0 <= row && row < rowCount() && 0 <= column && column < columnCount()) {
        return createIndex(row, column, const_cast<CNodeCombinedStats*>(&m_peers_data[row]));
    }

    return QModelIndex();
}

void PeerTableModel::refresh()
{
    interfaces::Node::NodesStats nodes_stats;
    m_node.getNodesStats(nodes_stats);
    decltype(m_peers_data) new_peers_data;
    new_peers_data.reserve(nodes_stats.size());
    for (const auto& node_stats : nodes_stats) {
        const CNodeCombinedStats stats{std::get<0>(node_stats), std::get<2>(node_stats), std::get<1>(node_stats)};
        new_peers_data.append(stats);
    }

    // Handle peer addition or removal as suggested in Qt Docs. See:
    // - https://doc.qt.io/qt-5/model-view-programming.html#inserting-and-removing-rows
    // - https://doc.qt.io/qt-5/model-view-programming.html#resizable-models
    // We take advantage of the fact that the std::vector returned
    // by interfaces::Node::getNodesStats is sorted by nodeid.
    for (int i = 0; i < m_peers_data.size();) {
        if (i < new_peers_data.size() && m_peers_data.at(i).nodeStats.nodeid == new_peers_data.at(i).nodeStats.nodeid) {
            ++i;
            continue;
        }
        // A peer has been removed from the table.
        beginRemoveRows(QModelIndex(), i, i);
        m_peers_data.erase(m_peers_data.begin() + i);
        endRemoveRows();
    }

    if (m_peers_data.size() < new_peers_data.size()) {
        // Some peers have been added to the end of the table.
        beginInsertRows(QModelIndex(), m_peers_data.size(), new_peers_data.size() - 1);
        m_peers_data.swap(new_peers_data);
        endInsertRows();
    } else {
        m_peers_data.swap(new_peers_data);
    }

    const auto top_left = index(0, 0);
    const auto bottom_right = index(rowCount() - 1, columnCount() - 1);
    Q_EMIT dataChanged(top_left, bottom_right);
}
