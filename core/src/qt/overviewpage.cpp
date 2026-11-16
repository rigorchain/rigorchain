// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/*
IMPORTANT: WALLET DESIGN LOCK V1

- RigorChain is a Dual-Native Asset system (ORU + RES)
- Address is asset-agnostic (rig1...), BUT wallet MUST enforce asset separation
- Wallet MUST NEVER:
    * mix ORU and RES in one transaction
    * auto-detect asset type
    * aggregate balances
- UTXO selection MUST be asset-specific
- Change output MUST match input asset
- Network fees are always ORU (POOL-F); transfer asset is separate (POOL-T)

CONVERGENCE (MINT) RULES:
- Mint inputs are ORU ONLY; the burn is one explicit OP_CONVERGE output of value B
- R RES is minted when R · (2^25+h)^2 <= B << 40 at the CONFIRMING height; R = 0 is a legal plain burn
- Wallet MUST take destroy_cost(h) from the node, never hardcode or re-derive it

ARCHITECTURE:
- Wallet constructs transactions ONLY
- Consensus defines destroy_cost(h) and mint authorization
- No policy logic inside wallet

RES RULES LOCK

- A mint is an ordinary transaction: no coinbase involvement and no payment lag
- Minted RES stays time-locked until RES_MINT_MATURITY (reorg protection on new supply, NOT an outcome wait);
  the lock applies to mints only, never to RES transfers or RES change
- Any number of mints may appear in one block, bounded only by block weight
- Fractional purchase is permitted: R is any positive number of RES atoms

UI MUST:

- show the exact R the mint will create, and that the burned ORU is unrecoverable
- distinguish AVAILABLE vs locked RES (mempool RES change folded into Total only)
- NOT present the rising destroy_cost(h) curve as gain, appreciation or timing pressure
- NOT show any outcome state (Won / Lost / Not selected); none exists

WALLET_SAFETY_BOUNDARY_V1 (see wallet/wallet_safety_boundary.h)
*/

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/rigorwallet/widgets/orubalancepanel.h>
#include <qt/rigorwallet/widgets/resbalancepanel.h>
#include <qt/rigorwallet/assetbadge.h>
#include <qt/rigorwallet/assetcontext.h>
#include <qt/rigorwallet/rigortheme.h>
#include <qt/rigorwallet/walletidentity.h>

#include <primitives/transaction.h>
#include <qt/bitcoinamountfield.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include "interfaces/node.h"
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactionoverviewwidget.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <QAbstractItemDelegate>
#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>
#include <wallet/wallet_safety_boundary.h>
#include <wallet/tx_semantic.h>
#include <QPainter>
#include <QStatusTipEvent>

#include <algorithm>
#include <map>

#define DECORATION_SIZE 54
#define NUM_ITEMS 5

Q_DECLARE_METATYPE(interfaces::WalletBalances)

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle* _platformStyle, QObject* parent = nullptr)
        : QAbstractItemDelegate(parent), platformStyle(_platformStyle)
    {
        connect(this, &TxViewDelegate::width_changed, this, &TxViewDelegate::sizeHintChanged);
    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const override
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        // Always recolor with WindowText so mined/pickaxe icons stay visible on dark chrome.
        icon = platformStyle->TextColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        if (index.data(TransactionTableModel::WatchonlyRole).toBool()) {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(addressRect.left(), addressRect.top(), 16, addressRect.height());
            iconWatchonly = platformStyle->TextColorIcon(iconWatchonly);
            iconWatchonly.paint(painter, watchonlyRect);
            addressRect.setLeft(addressRect.left() + watchonlyRect.width() + 5);
        }

        painter->setPen(foreground);
        const wallet::TxSemanticKind semantic = static_cast<wallet::TxSemanticKind>(
            index.data(TransactionTableModel::SemanticKindRole).toInt());
        const RigorWallet::AssetBadgeKind badgeKind = RigorWallet::AssetContext::badgeKindFromSemantic(semantic);
        QString addressLine = address;
        if (badgeKind != RigorWallet::AssetBadgeKind::Unknown) {
            addressLine = RigorWallet::AssetBadge::labelForKind(badgeKind) + QStringLiteral("  ") + address;
        }
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft | Qt::AlignVCenter, addressLine, &boundingRect);

        if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        const AssetType row_asset = static_cast<AssetType>(
            index.data(TransactionTableModel::AssetDomainRole).toInt());
        QString amountText = BitcoinUnits::formatAmountForAsset(
            row_asset, amount, unit, true, BitcoinUnits::SeparatorStyle::ALWAYS, true);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }

        QRect amount_bounding_rect;
        painter->drawText(amountRect, Qt::AlignRight | Qt::AlignVCenter, amountText, &amount_bounding_rect);

        painter->setPen(option.palette.color(QPalette::Text));
        QRect date_bounding_rect;
        painter->drawText(amountRect, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::dateTimeStr(date), &date_bounding_rect);

        // 0.4*date_bounding_rect.width() is used to visually distinguish a date from an amount.
        const int minimum_width = 1.4 * date_bounding_rect.width() + amount_bounding_rect.width();
        const auto search = m_minimum_width.find(index.row());
        if (search == m_minimum_width.end() || search->second != minimum_width) {
            m_minimum_width[index.row()] = minimum_width;
            Q_EMIT width_changed(index);
        }

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto search = m_minimum_width.find(index.row());
        const int minimum_text_width = search == m_minimum_width.end() ? 0 : search->second;
        return {DECORATION_SIZE + 8 + minimum_text_width, DECORATION_SIZE};
    }

    BitcoinUnit unit{BitcoinUnit::ORU};

Q_SIGNALS:
    //! An intermediate signal for emitting from the `paint() const` member function.
    void width_changed(const QModelIndex& index) const;

private:
    const PlatformStyle* platformStyle;
    mutable std::map<int, int> m_minimum_width;
};

#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    m_platform_style{platformStyle},
    txdelegate(new TxViewDelegate(platformStyle, this))
{
    ui->setupUi(this);

    m_syncStatusBanner = new QLabel(this);
    m_syncStatusBanner->setObjectName(QStringLiteral("RigorSyncStatusBanner"));
    m_syncStatusBanner->setWordWrap(true);
    m_syncStatusBanner->setText(tr(
        "<b>Blockchain Status</b><br>"
        "Waiting for synchronization…<br>"
        "Network: <b>%1</b><br>"
        "RES Convergence: Available after ORU acquisition")
        .arg(RigorWallet::WalletIdentity::networkDisplayName()));
    m_syncStatusBanner->setStyleSheet(QStringLiteral(
        "QLabel#RigorSyncStatusBanner {"
        "  background: #EADCC6; color: #26394B; border: 1px solid #8A7E6C;"
        "  border-radius: 6px; padding: 10px;"
        "}"));
    ui->topLayout->insertWidget(0, m_syncStatusBanner);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = m_platform_style->SingleColorIcon(QStringLiteral(":/icons/warning"));
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, &TransactionOverviewWidget::clicked, this, &OverviewPage::handleTransactionClicked);

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, &QPushButton::clicked, this, &OverviewPage::outOfSyncWarningClicked);
    connect(ui->labelTransactionsStatus, &QPushButton::clicked, this, &OverviewPage::outOfSyncWarningClicked);

    ui->groupDualAssets->setVisible(false);

    m_oruPanel = new RigorWallet::ORUBalancePanel(this);
    m_resPanel = new RigorWallet::RESBalancePanel(this);

    auto* dualAssetHost = new QWidget(this);
    auto* dualLayout = new QVBoxLayout(dualAssetHost);
    dualLayout->setContentsMargins(0, 0, 0, 0);
    dualLayout->setSpacing(RigorWallet::RigorTheme::spacing().sectionGap);
    dualLayout->addWidget(m_oruPanel);
    dualLayout->addWidget(m_resPanel);

    if (ui->verticalLayout_4) {
        ui->verticalLayout_4->insertWidget(2, dualAssetHost);
    }
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::setPrivacy(bool privacy)
{
    m_privacy = privacy;
    clientModel->getOptionsModel()->setOption(OptionsModel::OptionID::MaskValues, privacy);
    const auto& balances = walletModel->getCachedBalance();
    if (balances.balance != -1) {
        setBalance(balances);
    }

    ui->listTransactions->setVisible(!m_privacy);

    const QString status_tip = m_privacy ? tr("Privacy mode activated for the Overview tab. To unmask the values, uncheck Settings->Mask values.") : "";
    setStatusTip(status_tip);
    QStatusTipEvent event(status_tip);
    QApplication::sendEvent(this, &event);
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const interfaces::WalletBalances& balances)
{
    if (!walletModel || !walletModel->getOptionsModel()) return;
    BitcoinUnit unit = walletModel->getOptionsModel()->getDisplayUnit();

    if (m_oruPanel) {
        m_oruPanel->setBalances(balances, unit, m_privacy);
    }
    if (m_resPanel) {
        m_resPanel->setBalances(balances, m_privacy);
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    (void)showWatchOnly;
    // Legacy spendable/watch-only grid removed; balances are shown only under Protocol Primitives.
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if (model) {
        // Show warning, for example if this is a prerelease version
        connect(model, &ClientModel::alertsChanged, this, &OverviewPage::updateAlerts);
        updateAlerts(model->getStatusBarWarnings());

        connect(model->getOptionsModel(), &OptionsModel::useEmbeddedMonospacedFontChanged, this, &OverviewPage::setMonospacedFont);
        setMonospacedFont(model->getOptionsModel()->getUseEmbeddedMonospacedFont());

        connect(model->getOptionsModel(), &OptionsModel::defaultSendAssetChanged, this, &OverviewPage::updateDefaultSendAssetDisplay);

    }
    updateDefaultSendAssetDisplay();
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        connect(filter.get(), &TransactionFilterProxy::rowsInserted, this, &OverviewPage::LimitTransactionRows);
        connect(filter.get(), &TransactionFilterProxy::rowsRemoved, this, &OverviewPage::LimitTransactionRows);
        connect(filter.get(), &TransactionFilterProxy::rowsMoved, this, &OverviewPage::LimitTransactionRows);
        LimitTransactionRows();
        // Keep up to date with wallet
        setBalance(model->getCachedBalance());
        connect(model, &WalletModel::balanceChanged, this, &OverviewPage::setBalance);
        connect(model, &WalletModel::balanceSnapshotStaleChanged, this, [this](bool stale) {
            if (m_oruPanel) {
                m_oruPanel->setToolTip(stale
                    ? tr("Wallet is busy updating; amounts may briefly lag behind the chain.")
                    : QString());
            }
        });

        m_resPanel->setWalletModel(model);

        connect(model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &OverviewPage::updateDisplayUnit);

        interfaces::Wallet& wallet = model->wallet();
        updateWatchOnlyLabels(wallet.haveWatchOnly() && !wallet.privateKeysDisabled());
        connect(model, &WalletModel::notifyWatchonlyChanged, [this](bool showWatchOnly) {
            updateWatchOnlyLabels(showWatchOnly && !walletModel->wallet().privateKeysDisabled());
        });
    }

    // update the display unit, to not use the default ("ORU")
    updateDisplayUnit();
    updateDefaultSendAssetDisplay();
}

void OverviewPage::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        QIcon icon = m_platform_style->SingleColorIcon(QStringLiteral(":/icons/warning"));
        ui->labelTransactionsStatus->setIcon(icon);
        ui->labelWalletStatus->setIcon(icon);
    }

    QWidget::changeEvent(e);
}

// Only show most recent NUM_ITEMS rows
void OverviewPage::LimitTransactionRows()
{
    if (filter && ui->listTransactions && ui->listTransactions->model() && filter.get() == ui->listTransactions->model()) {
        for (int i = 0; i < filter->rowCount(); ++i) {
            ui->listTransactions->setRowHidden(i, i >= NUM_ITEMS);
        }
    }
}

void OverviewPage::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        const auto& balances = walletModel->getCachedBalance();
        if (balances.balance != -1) {
            setBalance(balances);
        }

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
    if (m_syncStatusBanner) {
        m_syncStatusBanner->setVisible(fShow);
    }
}

void OverviewPage::setMonospacedFont(bool use_embedded_font)
{
    (void)use_embedded_font;
}

void OverviewPage::updateDefaultSendAssetDisplay()
{
    OptionsModel* opts = nullptr;
    if (clientModel && clientModel->getOptionsModel()) opts = clientModel->getOptionsModel();
    else if (walletModel && walletModel->getOptionsModel()) opts = walletModel->getOptionsModel();
    if (!opts) {
        ui->labelDefaultSendAsset->setText(tr("—"));
        return;
    }
    const AssetType a = opts->getDefaultSendAsset();
    ui->labelDefaultSendAsset->setText(RigorWallet::AssetContext::shortLabel(
        RigorWallet::AssetContext::fromAssetType(a)));
    ui->labelDefaultSendAsset->setToolTip(tr("Global default for the Send tab and new sends. rig1… addresses do not encode the asset; always confirm asset and amount."));
}
