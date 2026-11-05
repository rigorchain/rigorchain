// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/dualitystatisticspage.h>

#include <index/economicsindex.h>
#include <qt/bitcoinunits.h>
#include <qt/rigorwallet/rigortheme.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const QString kNA = QStringLiteral("N/A");
const QString kStale = QStringLiteral("Stale Data");
const QString kRefreshing = QStringLiteral("Refreshing");
const QString kSyncing = QStringLiteral("Syncing");
const QString kUnavailable = QStringLiteral("Index Unavailable");
const QString kRebuilding = QStringLiteral("Index Rebuilding");
const QString kSynced = QStringLiteral("Synced");
const QString kCurrent = QStringLiteral("Current");
const QString kCaughtUp = QStringLiteral("Caught up");
const QString kNoUpdate = QStringLiteral("No successful update yet");

QString amountText(AssetType asset, CAmount amount)
{
    return BitcoinUnits::formatAmountForAsset(asset, amount, BitcoinUnits::Unit::ORU);
}

QString intervalValue()
{
    return QStringLiteral("%1 seconds").arg(static_cast<int>(ECONOMICS_REFRESH_INTERVAL.count()));
}

QString intervalPhrase()
{
    return QStringLiteral("Automatic refresh: %1").arg(intervalValue());
}

QString timestampText(int64_t generated_at)
{
    return QDateTime::fromSecsSinceEpoch(generated_at, Qt::UTC).toString(Qt::ISODate) + QStringLiteral(" UTC");
}

bool warningWord(const QString& text)
{
    return text == kStale || text == kRebuilding;
}

QLabel* styledLabel(QWidget* parent, const QString& text, int pixel_size, bool bold, const QColor& color, Qt::Alignment alignment)
{
    auto* label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setAlignment(alignment);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: %2px; font-weight: %3;")
                             .arg(RigorWallet::RigorTheme::colorHex(color))
                             .arg(pixel_size)
                             .arg(bold ? QStringLiteral("700") : QStringLiteral("400")));
    return label;
}

QFrame* makeCard(QWidget* parent, const QString& frame_name, const QString& title, const QString& meaning, const QString& unit,
    const QString& value_name, const QString& title_name, QLabel** value_out, const QColor& accent)
{
    const RigorWallet::ColorPalette colors = RigorWallet::RigorTheme::colors();
    const RigorWallet::Typography type = RigorWallet::RigorTheme::typography();
    const RigorWallet::Spacing space = RigorWallet::RigorTheme::spacing();
    auto* frame = new QFrame(parent);
    frame->setObjectName(frame_name);
    frame->setMinimumHeight(128);
    frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    frame->setStyleSheet(QStringLiteral(
                             "QFrame#%1 { background-color: %2; border: 1px solid %3; border-top: %4px solid %5; border-radius: %6px; }")
                             .arg(frame_name)
                             .arg(RigorWallet::RigorTheme::colorHex(colors.bgElevated))
                             .arg(RigorWallet::RigorTheme::colorHex(colors.borderSubtle))
                             .arg(space.assetBorderWidth)
                             .arg(RigorWallet::RigorTheme::colorHex(accent))
                             .arg(space.borderRadiusPanel));
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(space.cardPadding, space.cardPadding, space.cardPadding, space.cardPadding);
    layout->setSpacing(space.unit / 2);
    QLabel* title_label = styledLabel(frame, title, type.labelPx, false, colors.textMuted, Qt::AlignLeft | Qt::AlignTop);
    title_label->setObjectName(title_name);
    auto* value = styledLabel(frame, kNA, type.balancePx, true, colors.textPrimary, Qt::AlignRight | Qt::AlignVCenter);
    value->setObjectName(value_name);
    QLabel* unit_label = styledLabel(frame, unit, type.labelPx, false, colors.textMuted, Qt::AlignRight | Qt::AlignVCenter);
    QLabel* meaning_label = styledLabel(frame, meaning, type.labelPx, false, colors.textMuted, Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(title_label);
    layout->addStretch();
    layout->addWidget(value);
    layout->addWidget(unit_label);
    layout->addWidget(meaning_label);
    *value_out = value;
    return frame;
}

} // namespace

DualityStatisticsPage::DualityStatisticsPage(QWidget* parent)
    : QWidget(parent)
{
    const RigorWallet::ColorPalette colors = RigorWallet::RigorTheme::colors();
    const RigorWallet::Typography type = RigorWallet::RigorTheme::typography();
    const RigorWallet::Spacing space = RigorWallet::RigorTheme::spacing();
    setObjectName(QStringLiteral("DualityStatisticsPage"));
    setStyleSheet(QStringLiteral("QWidget#DualityStatisticsPage { background-color: %1; }")
                      .arg(RigorWallet::RigorTheme::colorHex(colors.bgPrimary)));

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: %1; border: none; } QScrollArea > QWidget > QWidget { background: %1; }")
                              .arg(RigorWallet::RigorTheme::colorHex(colors.bgPrimary)));
    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(space.cardPadding, space.cardPadding, space.cardPadding, space.cardPadding);
    layout->setSpacing(space.sectionGap);

    auto* header = new QHBoxLayout();
    header->setSpacing(space.unit);
    QLabel* title = styledLabel(body, QStringLiteral("Duality Statistics"), type.headingPx, true, colors.textPrimary, Qt::AlignLeft | Qt::AlignVCenter);
    title->setObjectName(QStringLiteral("dualityTitle"));
    auto* refresh = new QPushButton(QStringLiteral("Refresh now"), body);
    refresh->setObjectName(QStringLiteral("dualityRefreshButton"));
    connect(refresh, &QPushButton::clicked, this, [this] { startRefresh(true); });
    header->addWidget(title, 1);
    header->addWidget(refresh, 0, Qt::AlignRight | Qt::AlignVCenter);
    layout->addLayout(header);

    m_refreshed = styledLabel(body, kNoUpdate, type.labelPx, false, colors.textMuted, Qt::AlignLeft | Qt::AlignVCenter);
    m_refreshed->setObjectName(QStringLiteral("dualityRefreshed"));
    layout->addWidget(m_refreshed);
    m_status = styledLabel(body, intervalPhrase() + QStringLiteral(" · ") + kSyncing, type.labelPx, false, colors.textMuted, Qt::AlignLeft | Qt::AlignVCenter);
    m_status->setObjectName(QStringLiteral("dualityStatus"));
    layout->addWidget(m_status);

    QLabel* oru_title = styledLabel(body, QStringLiteral("ORU Statistics"), type.uiBodyLargePx, true, colors.textPrimary, Qt::AlignLeft);
    oru_title->setObjectName(QStringLiteral("oruModuleTitle"));
    layout->addWidget(oru_title);
    auto* oru_host = new QWidget(body);
    m_oru_grid = new QGridLayout(oru_host);
    m_oru_grid->setContentsMargins(0, 0, 0, 0);
    m_oru_grid->setHorizontalSpacing(space.sectionGap);
    m_oru_grid->setVerticalSpacing(space.sectionGap);
    m_oru_cards[0] = makeCard(oru_host, QStringLiteral("oruCard0"), QStringLiteral("Block Rewards Issued"),
        QStringLiteral("Total ORU block rewards issued."), QStringLiteral("ORU"), QStringLiteral("oruScheduleValue"),
        QStringLiteral("oruScheduleTitle"), &m_oru_schedule, colors.assetOru);
    m_oru_cards[1] = makeCard(oru_host, QStringLiteral("oruCard1"), QStringLiteral("Convergence Burned"),
        QStringLiteral("Total ORU destroyed through Convergence."), QStringLiteral("ORU"), QStringLiteral("oruConvergenceValue"),
        QStringLiteral("oruConvergenceTitle"), &m_oru_convergence, colors.assetOru);
    m_oru_cards[2] = makeCard(oru_host, QStringLiteral("oruCard2"), QStringLiteral("Transaction Fee Burned"),
        QStringLiteral("ORU destroyed through RES transaction fees."), QStringLiteral("ORU"), QStringLiteral("oruFeeValue"),
        QStringLiteral("oruFeeTitle"), &m_oru_fee, colors.assetOru);
    m_oru_cards[3] = makeCard(oru_host, QStringLiteral("oruCard3"), QStringLiteral("Current ORU UTXO Supply"),
        QStringLiteral("Current ORU amount represented by UTXOs."), QStringLiteral("ORU"), QStringLiteral("oruUtxoValue"),
        QStringLiteral("oruUtxoTitle"), &m_oru_utxo, colors.assetOru);
    layout->addWidget(oru_host);

    QLabel* res_title = styledLabel(body, QStringLiteral("RES Statistics"), type.uiBodyLargePx, true, colors.textPrimary, Qt::AlignLeft);
    res_title->setObjectName(QStringLiteral("resModuleTitle"));
    layout->addWidget(res_title);
    auto* res_host = new QWidget(body);
    m_res_grid = new QGridLayout(res_host);
    m_res_grid->setContentsMargins(0, 0, 0, 0);
    m_res_grid->setHorizontalSpacing(space.sectionGap);
    m_res_grid->setVerticalSpacing(space.sectionGap);
    m_res_cards[0] = makeCard(res_host, QStringLiteral("resCard0"), QStringLiteral("Current RES UTXO Supply"),
        QStringLiteral("Current RES amount represented by UTXOs."), QStringLiteral("RES"),
        QStringLiteral("resSupplyValue"), QStringLiteral("resSupplyTitle"), &m_res_supply, colors.assetRes);
    m_res_cards[1] = makeCard(res_host, QStringLiteral("resCard1"), QStringLiteral("Remaining RES Issuance Capacity"),
        QStringLiteral("RES maximum supply minus total RES minted."), QStringLiteral("RES"),
        QStringLiteral("resRemainingValue"), QStringLiteral("resRemainingTitle"), &m_res_remaining, colors.assetRes);
    layout->addWidget(res_host);

    QLabel* status_title = styledLabel(body, QStringLiteral("Index & Data Status"), type.uiBodyLargePx, true, colors.textPrimary, Qt::AlignLeft);
    status_title->setObjectName(QStringLiteral("statusModuleTitle"));
    layout->addWidget(status_title);
    auto* status_panel = new QFrame(body);
    status_panel->setObjectName(QStringLiteral("dualityStatusPanel"));
    status_panel->setStyleSheet(QStringLiteral(
                                    "QFrame#dualityStatusPanel { background-color: %1; border: 1px solid %2; border-radius: %3px; }")
                                    .arg(RigorWallet::RigorTheme::colorHex(colors.bgSurface))
                                    .arg(RigorWallet::RigorTheme::colorHex(colors.borderSubtle))
                                    .arg(space.borderRadiusPanel));
    m_status_grid = new QGridLayout(status_panel);
    m_status_grid->setContentsMargins(space.panelPadding, space.panelPadding, space.panelPadding, space.panelPadding);
    m_status_grid->setHorizontalSpacing(space.sectionGap);
    m_status_grid->setVerticalSpacing(space.unit);
    const QString status_names[7] = {
        QStringLiteral("Local Economics Index"),
        QStringLiteral("Index synchronization"),
        QStringLiteral("Indexed block"),
        QStringLiteral("Active chain"),
        QStringLiteral("Last successful update"),
        QStringLiteral("Automatic refresh"),
        QStringLiteral("Current data freshness"),
    };
    const QString value_names[7] = {
        QStringLiteral("dualityIndexState"),
        QStringLiteral("dualitySyncState"),
        QStringLiteral("dualityIndexedHeight"),
        QStringLiteral("dualityActiveHeight"),
        QStringLiteral("dualityLastUpdate"),
        QStringLiteral("dualityInterval"),
        QStringLiteral("dualityFreshness"),
    };
    const QString initial_values[7] = {kSyncing, kSyncing, kNA, kNA, kNoUpdate, intervalValue(), kSyncing};
    for (int i = 0; i < 7; ++i) {
        m_status_keys[i] = styledLabel(status_panel, status_names[i], type.labelPx, false, colors.textMuted, Qt::AlignLeft | Qt::AlignVCenter);
        m_status_values[i] = styledLabel(status_panel, initial_values[i], type.uiBodyPx, false, colors.textPrimary, Qt::AlignLeft | Qt::AlignVCenter);
        m_status_values[i]->setObjectName(value_names[i]);
    }
    m_index_state = m_status_values[0];
    m_sync_state = m_status_values[1];
    m_indexed_height = m_status_values[2];
    m_active_height = m_status_values[3];
    m_last_update = m_status_values[4];
    m_interval = m_status_values[5];
    m_freshness = m_status_values[6];
    layout->addWidget(status_panel);

    auto* notes = styledLabel(body,
        QStringLiteral("Notes\n"
                       "Block rewards are the subsidy schedule. Genesis is 0. Later blocks are 128 ORU. This is not a supply cap and not the UTXO balance.\n"
                       "Convergence Burned counts positive OP_CONVERGE outputs, including the coinbase. It is not circulating supply.\n"
                       "Transaction Fee Burned is the RES-transfer fee burn. It is not Convergence, and it is hidden as N/A until every ancestor fee is known.\n"
                       "Current ORU UTXO Supply is ORU still in the UTXO set. It is not block rewards minus burned ORU.\n"
                       "Remaining RES Issuance Capacity is the consensus RES maximum supply minus total RES minted."),
        type.labelPx, false, colors.textMuted, Qt::AlignLeft | Qt::AlignTop);
    notes->setObjectName(QStringLiteral("dualityNotes"));
    layout->addWidget(notes);
    layout->addStretch();

    scroll->setWidget(body);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
    relayout();

    m_timer = new QTimer(this);
    m_timer->setObjectName(QStringLiteral("dualityRefreshTimer"));
    m_timer->setInterval(static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(ECONOMICS_REFRESH_INTERVAL).count()));
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [this] { startRefresh(false); });
}

DualityStatisticsPage::~DualityStatisticsPage()
{
    m_closing.store(true);
    if (m_timer != nullptr) m_timer->stop();
    if (m_worker.joinable()) m_worker.join();
    QCoreApplication::removePostedEvents(this);
}

void DualityStatisticsPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_timer != nullptr && !m_timer->isActive()) m_timer->start();
    startRefresh(false);
}

void DualityStatisticsPage::hideEvent(QHideEvent* event)
{
    if (m_timer != nullptr) m_timer->stop();
    QWidget::hideEvent(event);
}

void DualityStatisticsPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void DualityStatisticsPage::relayout()
{
    if (m_oru_grid == nullptr || m_res_grid == nullptr || m_status_grid == nullptr) return;
    const int w = qMax(width(), 1);
    const int oru_cols = w >= 960 ? 4 : (w >= 640 ? 2 : 1);
    const int res_cols = w >= 640 ? 2 : 1;
    const int status_cols = w >= 640 ? 2 : 1;
    if (oru_cols == m_oru_cols && res_cols == m_res_cols && status_cols == m_status_cols) return;

    const auto place_cards = [](QGridLayout* grid, QWidget* const* cards, int count, int columns) {
        while (grid->count() > 0) {
            grid->takeAt(0);
        }
        for (int i = 0; i < count; ++i) {
            grid->addWidget(cards[i], i / columns, i % columns);
        }
        for (int column = 0; column < 4; ++column) {
            grid->setColumnStretch(column, column < columns ? 1 : 0);
        }
    };
    place_cards(m_oru_grid, m_oru_cards, 4, oru_cols);
    place_cards(m_res_grid, m_res_cards, 2, res_cols);
    while (m_status_grid->count() > 0) {
        m_status_grid->takeAt(0);
    }
    for (int i = 0; i < 7; ++i) {
        const int row = i / status_cols;
        const int column = (i % status_cols) * 2;
        m_status_grid->addWidget(m_status_keys[i], row, column);
        m_status_grid->addWidget(m_status_values[i], row, column + 1);
    }
    for (int column = 0; column < 4; ++column) {
        m_status_grid->setColumnStretch(column, column < status_cols * 2 && (column % 2) == 1 ? 1 : 0);
    }
    m_oru_cols = oru_cols;
    m_res_cols = res_cols;
    m_status_cols = status_cols;
}

void DualityStatisticsPage::setAmount(QLabel* label, bool available, const QString& text)
{
    label->setText(available ? text : kNA);
}

void DualityStatisticsPage::setStatusText(QLabel* label, const QString& text)
{
    label->setText(text);
    const RigorWallet::ColorPalette colors = RigorWallet::RigorTheme::colors();
    const RigorWallet::Typography type = RigorWallet::RigorTheme::typography();
    const QColor color = warningWord(text) ? colors.warning : colors.textPrimary;
    label->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: %2px;")
                             .arg(RigorWallet::RigorTheme::colorHex(color))
                             .arg(type.uiBodyPx));
}

void DualityStatisticsPage::showFreshness(const QString& word)
{
    m_status->setText(intervalPhrase() + QStringLiteral(" · ") + word);
    const RigorWallet::ColorPalette colors = RigorWallet::RigorTheme::colors();
    const RigorWallet::Typography type = RigorWallet::RigorTheme::typography();
    const QColor color = warningWord(word) ? colors.warning : colors.textMuted;
    m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent; font-size: %2px;")
                                .arg(RigorWallet::RigorTheme::colorHex(color))
                                .arg(type.labelPx));
    setStatusText(m_freshness, word);
}

void DualityStatisticsPage::startRefresh(bool manual)
{
    if (m_closing.load()) return;
    if (m_inflight.load()) {
        if (manual && m_have_authoritative) showFreshness(kRefreshing);
        return;
    }
    if (m_worker.joinable()) m_worker.join();
    m_inflight.store(true);
    if (m_have_authoritative) showFreshness(kRefreshing);
    m_worker = std::thread([this, manual] {
        const EconomicsSnapshot snap = QueryEconomicsSnapshot(manual);
        if (m_closing.load()) return;
        QMetaObject::invokeMethod(this, [this, snap] {
            if (m_closing.load()) {
                m_inflight.store(false);
                return;
            }
            applySnapshot(snap);
            m_inflight.store(false);
        }, Qt::QueuedConnection);
    });
}

void DualityStatisticsPage::applySnapshot(const EconomicsSnapshot& snapshot)
{
    const bool keep = m_have_authoritative &&
                      (snapshot.status == EconomicsStatus::Busy || snapshot.status == EconomicsStatus::RefreshFailed) &&
                      snapshot.amounts_authoritative;
    if (snapshot.amounts_authoritative) m_have_authoritative = true;

    QString index_word = kUnavailable;
    QString sync_word = kNA;
    QString fresh_word = kSyncing;
    QString updated = kNoUpdate;
    QString updated_value = kNoUpdate;
    bool show_amounts = false;
    const auto stamp = [&]() {
        if (snapshot.generated_at > 0) {
            const QString when = timestampText(snapshot.generated_at);
            updated = QStringLiteral("Last successful update: %1").arg(when);
            updated_value = when;
        }
    };

    switch (snapshot.status) {
    case EconomicsStatus::Disabled:
    case EconomicsStatus::Empty:
    case EconomicsStatus::Unavailable:
    case EconomicsStatus::Corrupt:
        index_word = kUnavailable;
        sync_word = kNA;
        fresh_word = kUnavailable;
        break;
    case EconomicsStatus::InitialBlockDownload:
        index_word = kSyncing;
        sync_word = kSyncing;
        fresh_word = kSyncing;
        break;
    case EconomicsStatus::Rebuilding:
        index_word = kRebuilding;
        sync_word = kSyncing;
        fresh_word = kRebuilding;
        break;
    case EconomicsStatus::Ok:
        index_word = kSynced;
        sync_word = kCaughtUp;
        fresh_word = kCurrent;
        show_amounts = true;
        stamp();
        break;
    case EconomicsStatus::Lagging:
        index_word = kSynced;
        sync_word = QStringLiteral("Lag: %1 blocks").arg(snapshot.lag_blocks);
        fresh_word = kCurrent;
        show_amounts = true;
        stamp();
        break;
    case EconomicsStatus::Busy:
        if (keep) {
            index_word = kSynced;
            sync_word = snapshot.lag_blocks > 0 ? QStringLiteral("Lag: %1 blocks").arg(snapshot.lag_blocks) : kCaughtUp;
            fresh_word = kRefreshing;
            show_amounts = true;
            stamp();
        } else {
            index_word = kSyncing;
            sync_word = kSyncing;
            fresh_word = kRefreshing;
        }
        break;
    case EconomicsStatus::RefreshFailed:
        index_word = kStale;
        sync_word = kStale;
        fresh_word = kStale;
        if (keep) {
            show_amounts = true;
            stamp();
        } else {
            updated = kStale;
            updated_value = kStale;
        }
        break;
    case EconomicsStatus::Stale:
        index_word = kStale;
        sync_word = kStale;
        fresh_word = kStale;
        updated = kStale;
        updated_value = kStale;
        break;
    }

    m_refreshed->setText(updated);
    m_last_update->setText(updated_value);
    showFreshness(fresh_word);
    setStatusText(m_index_state, index_word);
    setStatusText(m_sync_state, sync_word);
    m_indexed_height->setText(snapshot.indexed_height >= 0 ? QStringLiteral("height %1").arg(snapshot.indexed_height) : kNA);
    m_active_height->setText(snapshot.active_height >= 0 ? QStringLiteral("height %1").arg(snapshot.active_height) : kNA);
    m_interval->setText(intervalValue());

    if (!show_amounts) {
        for (QLabel* label : {m_oru_schedule, m_oru_convergence, m_oru_fee, m_oru_utxo, m_res_supply, m_res_remaining}) {
            label->setText(kNA);
        }
        return;
    }

    const EconomicsAmounts& amounts = snapshot.amounts;
    setAmount(m_oru_schedule, amounts.subsidy_available, amountText(ASSET_ORU, amounts.subsidy));
    setAmount(m_oru_convergence, amounts.convergence_available, amountText(ASSET_ORU, amounts.convergence));
    setAmount(m_oru_fee, amounts.fee_burn_available, amountText(ASSET_ORU, amounts.fee_burn));
    setAmount(m_oru_utxo, amounts.oru_utxo_available, amountText(ASSET_ORU, amounts.oru_utxo));
    setAmount(m_res_supply, amounts.res_supply_available, amountText(ASSET_RES, amounts.res_supply));
    setAmount(m_res_remaining, amounts.res_remaining_available, amountText(ASSET_RES, amounts.res_remaining));
}
