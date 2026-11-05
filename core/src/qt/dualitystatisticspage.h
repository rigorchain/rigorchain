// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_DUALITYSTATISTICSPAGE_H
#define BITCOIN_QT_DUALITYSTATISTICSPAGE_H

#include <index/economics_snapshot.h>

#include <QWidget>

#include <atomic>
#include <thread>

class DualityStatisticsTests;
class QGridLayout;
class QLabel;
class QTimer;
class QWidget;

/** Local ORU/RES statistics. Separate from the Convergence operation page. */
class DualityStatisticsPage : public QWidget
{
    Q_OBJECT

public:
    explicit DualityStatisticsPage(QWidget* parent = nullptr);
    ~DualityStatisticsPage() override;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    friend class DualityStatisticsTests;

    void startRefresh(bool manual);
    void applySnapshot(const EconomicsSnapshot& snapshot);
    void setAmount(QLabel* label, bool available, const QString& text);
    void showFreshness(const QString& word);
    void setStatusText(QLabel* label, const QString& text);
    void relayout();

    QTimer* m_timer{nullptr};
    QGridLayout* m_oru_grid{nullptr};
    QGridLayout* m_res_grid{nullptr};
    QGridLayout* m_status_grid{nullptr};
    QWidget* m_oru_cards[4]{};
    QWidget* m_res_cards[2]{};
    QLabel* m_status_keys[7]{};
    QLabel* m_status_values[7]{};
    int m_oru_cols{0};
    int m_res_cols{0};
    int m_status_cols{0};

    QLabel* m_status{nullptr};
    QLabel* m_refreshed{nullptr};
    QLabel* m_index_state{nullptr};
    QLabel* m_sync_state{nullptr};
    QLabel* m_indexed_height{nullptr};
    QLabel* m_active_height{nullptr};
    QLabel* m_last_update{nullptr};
    QLabel* m_interval{nullptr};
    QLabel* m_freshness{nullptr};
    QLabel* m_oru_schedule{nullptr};
    QLabel* m_oru_convergence{nullptr};
    QLabel* m_oru_fee{nullptr};
    QLabel* m_oru_utxo{nullptr};
    QLabel* m_res_generated{nullptr};
    QLabel* m_res_supply{nullptr};
    std::thread m_worker;
    std::atomic<bool> m_inflight{false};
    std::atomic<bool> m_closing{false};
    bool m_have_authoritative{false};
};

#endif // BITCOIN_QT_DUALITYSTATISTICSPAGE_H
