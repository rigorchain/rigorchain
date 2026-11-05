// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/walletview.h>

#include <qt/rigorwallet/explorer/explorer_status_service.h>
#include <qt/rigorwallet/widgets/firstrunwizard.h>

#include <qt/addressbookpage.h>
#include <qt/askpassphrasedialog.h>
#include <qt/clientmodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/convergencepage.h>
#include <qt/dualitystatisticspage.h>
#include <qt/overviewpage.h>
#include <qt/platformstyle.h>
#include <qt/receivecoinsdialog.h>
#include <qt/sendcoinsdialog.h>
#include <qt/keymanagementdialog.h>
#include <qt/signverifymessagedialog.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <qt/guiconstants.h>

#include <interfaces/node.h>
#include <node/interface_ui.h>
#include <util/strencodings.h>

#include <QAction>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QProgressDialog>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <chrono>

WalletView::WalletView(WalletModel* wallet_model, const PlatformStyle* _platformStyle, QWidget* parent)
    : QStackedWidget(parent),
      walletModel(wallet_model),
      platformStyle(_platformStyle)
{
    assert(walletModel);

    // Create tabs (Bitcoin Core Qt path: all pages exist before the load dialog closes).
    overviewPage = new OverviewPage(platformStyle);
    overviewPage->setWalletModel(walletModel);

    convergencePage = new ConvergencePage(platformStyle);
    convergencePage->setWalletModel(walletModel);

    dualityStatisticsPage = new DualityStatisticsPage(this);

    transactionsPage = new QWidget(this);
    QVBoxLayout *vbox = new QVBoxLayout();
    QHBoxLayout *hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(platformStyle, this);
    transactionView->setModel(walletModel);

    vbox->addWidget(transactionView);
    QPushButton *exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    if (platformStyle->getImagesOnButtons()) {
        exportButton->setIcon(platformStyle->SingleColorIcon(":/icons/export"));
    }
    hbox_buttons->addStretch();
    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);
    transactionsPage->setLayout(vbox);

    receiveCoinsPage = new ReceiveCoinsDialog(platformStyle);
    receiveCoinsPage->setModel(walletModel);

    sendCoinsPage = new SendCoinsDialog(platformStyle);
    sendCoinsPage->setModel(walletModel);

    usedSendingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    usedSendingAddressesPage->setModel(walletModel->getAddressTableModel());

    usedReceivingAddressesPage = new AddressBookPage(platformStyle, AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);
    usedReceivingAddressesPage->setModel(walletModel->getAddressTableModel());

    addWidget(overviewPage);
    addWidget(transactionsPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);
    addWidget(convergencePage);
    addWidget(dualityStatisticsPage);

    connect(overviewPage, &OverviewPage::transactionClicked, this, &WalletView::transactionClicked);
    connect(overviewPage, &OverviewPage::transactionClicked, transactionView, qOverload<const QModelIndex&>(&TransactionView::focusTransaction));
    connect(overviewPage, &OverviewPage::outOfSyncWarningClicked, this, &WalletView::outOfSyncWarningClicked);

    connect(sendCoinsPage, &SendCoinsDialog::coinsSent, this, &WalletView::coinsSent);
    connect(sendCoinsPage, &SendCoinsDialog::coinsSent, transactionView, qOverload<const uint256&>(&TransactionView::focusTransaction));
    connect(exportButton, &QPushButton::clicked, transactionView, &TransactionView::exportClicked);
    connect(sendCoinsPage, &SendCoinsDialog::message, this, &WalletView::message);
    connect(transactionView, &TransactionView::message, this, &WalletView::message);

    connect(this, &WalletView::setPrivacy, overviewPage, &OverviewPage::setPrivacy);
    connect(this, &WalletView::setPrivacy, this, &WalletView::disableTransactionView);

    connect(walletModel, &WalletModel::message, this, &WalletView::message);
    connect(walletModel, &WalletModel::encryptionStatusChanged, this, &WalletView::encryptionStatusChanged);
    connect(walletModel->getTransactionTableModel(), &TransactionTableModel::rowsInserted, this, &WalletView::processNewTransaction);
    connect(walletModel, &WalletModel::requireUnlock, this, &WalletView::unlockWallet);
    connect(walletModel, &WalletModel::showProgress, this, &WalletView::showProgress);

    m_incoming_notify_timer = new QTimer(this);
    m_incoming_notify_timer->setSingleShot(true);
    connect(m_incoming_notify_timer, &QTimer::timeout, this, &WalletView::flushIncomingNotifications);
}

WalletView::~WalletView() = default;

void WalletView::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;

    overviewPage->setClientModel(_clientModel);
    convergencePage->setClientModel(_clientModel);
    RigorWallet::ExplorerStatusService::instance().setClientModel(_clientModel);
    sendCoinsPage->setClientModel(_clientModel);
    walletModel->setClientModel(_clientModel);

    maybeShowFirstRunWizard();
}

void WalletView::maybeShowFirstRunWizard()
{
    if (!clientModel || !walletModel) return;
    if (!RigorWallet::FirstRunWizard::shouldShow()) return;
    RigorWallet::FirstRunWizard wizard(clientModel, walletModel, this);
    wizard.exec();
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int end)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!clientModel || clientModel->node().isInitialBlockDownload()) {
        return;
    }

    TransactionTableModel *ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    for (int row = start; row <= end; ++row) {
        const QString date = ttm->index(row, TransactionTableModel::Date, parent).data().toString();
        const CAmount amount = ttm->index(row, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
        const QString type = ttm->index(row, TransactionTableModel::Type, parent).data().toString();
        const QModelIndex index = ttm->index(row, 0, parent);
        const QString address = ttm->data(index, TransactionTableModel::AddressRole).toString();
        const QString label = GUIUtil::HtmlEscape(ttm->data(index, TransactionTableModel::LabelRole).toString());
        const AssetType amount_asset = static_cast<AssetType>(
            ttm->data(index, TransactionTableModel::AssetDomainRole).toInt());

        IncomingNotifyBucket& bucket = (amount_asset == ASSET_RES) ? m_pending_res : m_pending_oru;
        bucket.count += 1;
        bucket.amount += amount;
        bucket.date = date;
        bucket.type = type;
        bucket.address = address;
        bucket.label = label;
    }

    if (!m_incoming_notify_timer->isActive()) {
        m_incoming_notify_timer->start(static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(INCOMING_TX_NOTIFY_DELAY).count()));
    }
}

void WalletView::flushIncomingNotifications()
{
    const QString walletName = GUIUtil::HtmlEscape(walletModel->getWalletName());
    const auto emit_bucket = [this, &walletName](AssetType asset, IncomingNotifyBucket& bucket) {
        if (bucket.count <= 0) return;
        if (bucket.count == 1) {
            Q_EMIT incomingTransaction(bucket.date, asset, bucket.amount, bucket.type,
                                       bucket.address, bucket.label, walletName);
        } else {
            Q_EMIT incomingTransaction(bucket.date, asset, bucket.amount,
                                       tr("%n new transactions", "", bucket.count),
                                       QString(), QString(), walletName);
        }
        bucket = {};
    };
    emit_bucket(ASSET_ORU, m_pending_oru);
    emit_bucket(ASSET_RES, m_pending_res);
}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
}

void WalletView::gotoHistoryPage()
{
    setCurrentWidget(transactionsPage);
}

void WalletView::gotoConvergencePage()
{
    setCurrentWidget(convergencePage);
}

void WalletView::gotoDualityStatisticsPage()
{
    setCurrentWidget(dualityStatisticsPage);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);

    if (!addr.isEmpty())
        sendCoinsPage->setAddress(addr);
}

void WalletView::gotoSignMessageTab(QString addr)
{
    // calls show() in showTab_SM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_SM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_SM(addr);
}

void WalletView::gotoVerifyMessageTab(QString addr)
{
    // calls show() in showTab_VM()
    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(platformStyle, this);
    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
    signVerifyMessageDialog->setModel(walletModel);
    signVerifyMessageDialog->showTab_VM(true);

    if (!addr.isEmpty())
        signVerifyMessageDialog->setAddress_VM(addr);
}

void WalletView::gotoKeyManagement()
{
    auto* dlg = new KeyManagementDialog(platformStyle, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModel(walletModel);
    GUIUtil::ShowModalDialogAsynchronously(dlg);
}

bool WalletView::handlePaymentRequest(const SendCoinsRecipient& recipient)
{
    return sendCoinsPage->handlePaymentRequest(recipient);
}

void WalletView::showOutOfSyncWarning(bool fShow)
{
    overviewPage->showOutOfSyncWarning(fShow);
}

void WalletView::encryptWallet()
{
    auto dlg = new AskPassphraseDialog(AskPassphraseDialog::Encrypt, this);
    dlg->setModel(walletModel);
    connect(dlg, &QDialog::finished, this, &WalletView::encryptionStatusChanged);
    GUIUtil::ShowModalDialogAsynchronously(dlg);
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        //: Name of the wallet data file format.
        tr("Wallet Data") + QLatin1String(" (*.dat)"), nullptr);

    if (filename.isEmpty())
        return;

    if (!walletModel->wallet().backupWallet(filename.toLocal8Bit().data())) {
        Q_EMIT message(tr("Backup Failed"), tr("There was an error trying to save the wallet data to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
        }
    else {
        Q_EMIT message(tr("Backup Successful"), tr("The wallet data was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void WalletView::changePassphrase()
{
    auto dlg = new AskPassphraseDialog(AskPassphraseDialog::ChangePass, this);
    dlg->setModel(walletModel);
    GUIUtil::ShowModalDialogAsynchronously(dlg);
}

void WalletView::unlockWallet()
{
    // Unlock wallet when requested by wallet model
    if (walletModel->getEncryptionStatus() == WalletModel::Locked) {
        AskPassphraseDialog dlg(AskPassphraseDialog::Unlock, this);
        dlg.setModel(walletModel);
        // A modal dialog must be synchronous here as expected
        // in the WalletModel::requestUnlock() function.
        dlg.exec();
    }
}

void WalletView::usedSendingAddresses()
{
    GUIUtil::bringToFront(usedSendingAddressesPage);
}

void WalletView::usedReceivingAddresses()
{
    GUIUtil::bringToFront(usedReceivingAddressesPage);
}

void WalletView::showProgress(const QString &title, int nProgress)
{
    if (nProgress == 0) {
        progressDialog = new QProgressDialog(title, tr("Cancel"), 0, 100);
        GUIUtil::PolishProgressDialog(progressDialog);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    } else if (nProgress == 100) {
        if (progressDialog) {
            progressDialog->close();
            progressDialog->deleteLater();
            progressDialog = nullptr;
        }
    } else if (progressDialog) {
        if (progressDialog->wasCanceled()) {
            getWalletModel()->wallet().abortRescan();
        } else {
            progressDialog->setValue(nProgress);
        }
    }
}

void WalletView::disableTransactionView(bool disable)
{
    transactionView->setDisabled(disable);
}
