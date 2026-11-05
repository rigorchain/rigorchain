// Copyright (c) 2015-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/wallettests.h>
#include <qt/test/util.h>

#include <wallet/coincontrol.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <key.h>
#include <key_io.h>
#include <qt/bitcoinamountfield.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/optionsmodel.h>
#include <qt/overviewpage.h>
#include <qt/platformstyle.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/receivecoinsdialog.h>
#include <qt/receiverequestdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <qt/convergencepage.h>
#include <qt/rigorwallet/widgets/convergence_wizard.h>
#include <consensus/res_issuance.h>
#include <wallet/mint_height_pad.h>
#include <test/util/setup_common.h>
#include <validation.h>
#include <wallet/wallet.h>

#include <cassert>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include <QEventLoop>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QTranslator>
#include <QCheckBox>
#include <QClipboard>
#include <QObject>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>
#include <QTest>

using wallet::AddWallet;
using wallet::CWallet;
using wallet::CreateMockWalletDatabase;
using wallet::RemoveWallet;
using wallet::FEATURE_LATEST;
using wallet::WALLET_FLAG_DESCRIPTORS;
using wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS;
using wallet::WalletContext;
using wallet::WalletDescriptor;
using wallet::WalletRescanReserver;

namespace
{
/** Rigor key_io encodes only witness destinations; bare PKHash encodes to "" and breaks SendCoins. */
static CTxDestination MakeTestWitnessDestination()
{
    CKey key;
    key.MakeNewKey(/*fCompressed=*/true);
    return GetDestinationForKey(key.GetPubKey(), OutputType::BECH32);
}

/** QLabel balance strings may omit thin separators; tests use ALWAYS which inserts U+2009. */
QString NormalizeDigitsForBalanceCompare(const QString& s)
{
    QString t = s.trimmed();
    t.remove(QChar(0x2009)); // THIN SPACE (BitcoinUnits / ORU)
    t.remove(QChar(0x202F)); // NARROW NO-BREAK SPACE
    t.remove(QChar(0x00A0)); // NO-BREAK SPACE
    t.remove(QLatin1Char(' ')); // formatWithPrivacy justify padding
    return t;
}

//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmSend(QString* text = nullptr, QMessageBox::StandardButton confirm_type = QMessageBox::Yes)
{
    QTimer::singleShot(0, [text, confirm_type]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("SendConfirmationDialog")) {
                SendConfirmationDialog* dialog = qobject_cast<SendConfirmationDialog*>(widget);
                if (text) *text = dialog->text();
                QAbstractButton* button = dialog->button(confirm_type);
                button->setEnabled(true);
                button->click();
            }
        }
    });
}

//! Send coins to address and return txid.
uint256 SendCoins(CWallet& wallet, SendCoinsDialog& sendCoinsDialog, const CTxDestination& address, CAmount amount, bool rbf,
                  QMessageBox::StandardButton confirm_type = QMessageBox::Yes)
{
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    entry->findChild<QValidatedLineEdit*>("payTo")->setText(QString::fromStdString(EncodeDestination(address)));
    entry->findChild<BitcoinAmountField*>("payAmount")->setValue(amount);
    sendCoinsDialog.findChild<QFrame*>("frameFee")
        ->findChild<QFrame*>("frameFeeSelection")
        ->findChild<QCheckBox*>("optInRBF")
        ->setCheckState(rbf ? Qt::Checked : Qt::Unchecked);
    uint256 txid;
    boost::signals2::scoped_connection c(wallet.NotifyTransactionChanged.connect([&txid](const uint256& hash, ChangeType status) {
        if (status == CT_NEW) txid = hash;
    }));
    ConfirmSend(/*text=*/nullptr, confirm_type);
    bool invoked = QMetaObject::invokeMethod(&sendCoinsDialog, "sendButtonClicked", Q_ARG(bool, false));
    assert(invoked);
    return txid;
}

//! Find index of txid in transaction list.
QModelIndex FindTx(const QAbstractItemModel& model, const uint256& txid)
{
    QString hash = QString::fromStdString(txid.ToString());
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
            return index;
        }
    }
    return {};
}

//! Invoke bumpfee on txid and check results.
void BumpFee(TransactionView& view, const uint256& txid, bool expectDisabled, std::string expectError, bool cancel)
{
    QTableView* table = view.findChild<QTableView*>("transactionView");
    QModelIndex index = FindTx(*table->selectionModel()->model(), txid);
    QVERIFY2(index.isValid(), "Could not find BumpFee txid");

    // Select row in table, invoke context menu, and make sure bumpfee action is
    // enabled or disabled as expected.
    QAction* action = view.findChild<QAction*>("bumpFeeAction");
    table->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    action->setEnabled(expectDisabled);
    table->customContextMenuRequested({});
    QCOMPARE(action->isEnabled(), !expectDisabled);

    action->setEnabled(true);
    QString text;
    if (expectError.empty()) {
        ConfirmSend(&text, cancel ? QMessageBox::Cancel : QMessageBox::Yes);
    } else {
        ConfirmMessage(&text, 0ms);
    }
    action->trigger();
    QVERIFY(text.indexOf(QString::fromStdString(expectError)) != -1);
    qApp->processEvents(QEventLoop::AllEvents, 50);
}

void CompareBalance(WalletModel& walletModel, CAmount expected_balance, QLabel* balance_label_to_check)
{
    QVERIFY(balance_label_to_check);
    BitcoinUnit unit = walletModel.getOptionsModel()->getDisplayUnit();
    QString balanceComparison = BitcoinUnits::formatWithUnit(unit, expected_balance, false, BitcoinUnits::SeparatorStyle::ALWAYS);
    QCOMPARE(NormalizeDigitsForBalanceCompare(balance_label_to_check->text()),
             NormalizeDigitsForBalanceCompare(balanceComparison));
}

// Verify the 'useAvailableBalance' functionality. With and without manually selected coins.
// Case 1: No coin control selected coins.
// 'useAvailableBalance' should fill the amount edit box with the total available balance
// Case 2: With coin control selected coins.
// 'useAvailableBalance' should fill the amount edit box with the sum of the selected coins values.
void VerifyUseAvailableBalance(SendCoinsDialog& sendCoinsDialog, const WalletModel& walletModel)
{
    // Verify first entry amount and "useAvailableBalance" button
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    QVERIFY(entries->count() == 1); // only one entry
    SendCoinsEntry* send_entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    QVERIFY(send_entry->getValue().amount == 0);
    // Now click "useAvailableBalance", check updated balance (the entire wallet balance should be set)
    Q_EMIT send_entry->useAvailableBalance(send_entry);
    QVERIFY(send_entry->getValue().amount == walletModel.getCachedBalance().balance);

    // Now manually select two coins and click on "useAvailableBalance". Then check updated balance
    // (only the sum of the selected coins should be set).
    int COINS_TO_SELECT = 2;
    auto coins = walletModel.wallet().listCoins();
    CAmount sum_selected_coins = 0;
    int selected = 0;
    QVERIFY(coins.size() == 1); // context check, coins received only on one destination
    for (const auto& [outpoint, tx_out] : coins.begin()->second) {
        sendCoinsDialog.getCoinControl()->Select(outpoint);
        sum_selected_coins += tx_out.txout.nValue;
        if (++selected == COINS_TO_SELECT) break;
    }
    QVERIFY(selected == COINS_TO_SELECT);

    // Now that we have 2 coins selected, "useAvailableBalance" should update the balance label only with
    // the sum of them.
    Q_EMIT send_entry->useAvailableBalance(send_entry);
    QVERIFY(send_entry->getValue().amount == sum_selected_coins);
    // Isolate later SendCoins from leftover coin-control selection and SFFO fill.
    sendCoinsDialog.getCoinControl()->UnSelectAll();
    send_entry->findChild<BitcoinAmountField*>("payAmount")->setValue(0);
    if (auto* sffo = send_entry->findChild<QCheckBox*>("checkboxSubtractFeeFromAmount")) {
        sffo->setCheckState(Qt::Unchecked);
    }
}

void SyncUpWallet(const std::shared_ptr<CWallet>& wallet, interfaces::Node& node)
{
    WalletRescanReserver reserver(*wallet);
    reserver.reserve();
    CWallet::ScanResult result = wallet->ScanForWalletTransactions(Params().GetConsensus().hashGenesisBlock, /*start_height=*/0, /*max_height=*/{}, reserver, /*fUpdate=*/true, /*save_progress=*/false);
    QCOMPARE(result.status, CWallet::ScanResult::SUCCESS);
    QCOMPARE(result.last_scanned_block, WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip()->GetBlockHash()));
    QVERIFY(result.last_failed_block.IsNull());
}

std::shared_ptr<CWallet> SetupWatchOnlyWallet(interfaces::Node& node, TestChain100Setup& test)
{
    // Alpha1 coinbases are P2WPKH. Legacy ImportPubKeys only watches P2PK.
    // Watch-only descriptor wallets must not call SetupDescriptorScriptPubKeyMans()
    // (that generates a seed and asserts on WALLET_FLAG_DISABLE_PRIVATE_KEYS).
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(node.context()->chain.get(), "", CreateMockWalletDatabase());
    wallet->LoadWallet();
    LOCK(wallet->cs_wallet);
    wallet->SetMinVersion(FEATURE_LATEST);
    wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);

    FlatSigningProvider provider;
    std::string error;
    std::unique_ptr<Descriptor> desc = Parse("wpkh(" + HexStr(test.coinbaseKey.GetPubKey()) + ")", provider, error, /* require_checksum=*/ false);
    assert(desc);
    // Watch-only: never persist secp secrets even if Parse populated provider.keys.
    provider.keys.clear();
    WalletDescriptor w_desc(std::move(desc), 0, 0, 1, 1);
    if (!wallet->AddWalletDescriptor(w_desc, provider, "", false)) assert(false);
    CTxDestination dest = GetDestinationForKey(test.coinbaseKey.GetPubKey(), OutputType::BECH32);
    wallet->SetAddressBook(dest, "", wallet::AddressPurpose::RECEIVE);
    const CBlockIndex* tip = WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip());
    wallet->SetLastBlockProcessed(tip->nHeight, tip->GetBlockHash());
    SyncUpWallet(wallet, node);
    return wallet;
}

std::shared_ptr<CWallet> SetupDescriptorsWallet(interfaces::Node& node, TestChain100Setup& test)
{
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(node.context()->chain.get(), "", CreateMockWalletDatabase());
    wallet->LoadWallet();
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet->SetupDescriptorScriptPubKeyMans();

    // Add the coinbase key
    FlatSigningProvider provider;
    std::string error;
    std::unique_ptr<Descriptor> desc = Parse("wpkh(" + EncodeSecret(test.coinbaseKey) + ")", provider, error, /* require_checksum=*/ false);
    assert(desc);
    WalletDescriptor w_desc(std::move(desc), 0, 0, 1, 1);
    if (!wallet->AddWalletDescriptor(w_desc, provider, "", false)) assert(false);
    CTxDestination dest = GetDestinationForKey(test.coinbaseKey.GetPubKey(), wallet->m_default_address_type);
    wallet->SetAddressBook(dest, "", wallet::AddressPurpose::RECEIVE);
    const CBlockIndex* tip = WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip());
    wallet->SetLastBlockProcessed(tip->nHeight, tip->GetBlockHash());
    SyncUpWallet(wallet, node);
    wallet->SetBroadcastTransactions(true);
    return wallet;
}

struct MiniGUI {
public:
    SendCoinsDialog sendCoinsDialog;
    TransactionView transactionView;
    OptionsModel optionsModel;
    std::unique_ptr<ClientModel> clientModel;
    std::unique_ptr<WalletModel> walletModel;

    MiniGUI(interfaces::Node& node, const PlatformStyle* platformStyle) : sendCoinsDialog(platformStyle), transactionView(platformStyle), optionsModel(node) {
        bilingual_str error;
        QVERIFY(optionsModel.Init(error));
        clientModel = std::make_unique<ClientModel>(node, &optionsModel);
    }

    void initModelForWallet(interfaces::Node& node, const std::shared_ptr<CWallet>& wallet, const PlatformStyle* platformStyle)
    {
        WalletContext& context = *node.walletLoader().context();
        AddWallet(context, wallet);
        walletModel = std::make_unique<WalletModel>(interfaces::MakeWallet(context, wallet), *clientModel, platformStyle);
        RemoveWallet(context, wallet, /* load_on_start= */ std::nullopt);
        sendCoinsDialog.setModel(walletModel.get());
        transactionView.setModel(walletModel.get());
    }

};

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     QT_QPA_PLATFORM=xcb     src/qt/test/test_rigor-qt  # Linux
//     QT_QPA_PLATFORM=windows src/qt/test/test_rigor-qt  # Windows
//     QT_QPA_PLATFORM=cocoa   src/qt/test/test_rigor-qt  # macOS
void TestGUI(interfaces::Node& node, const std::shared_ptr<CWallet>& wallet)
{
    // Create widgets for sending coins and listing transactions.
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    MiniGUI mini_gui(node, platformStyle.get());
    mini_gui.initModelForWallet(node, wallet, platformStyle.get());
    WalletModel& walletModel = *mini_gui.walletModel;

    // Dual-asset key model UX: default wallets report ORU HD + RES HD.
    QVERIFY(walletModel.wallet().oruHdEnabled());
    QVERIFY(walletModel.wallet().resHdEnabled());
    QVERIFY(walletModel.wallet().hdEnabled());
    QVERIFY(!walletModel.wallet().resDedicatedKeyMode());
    SendCoinsDialog& sendCoinsDialog = mini_gui.sendCoinsDialog;
    TransactionView& transactionView = mini_gui.transactionView;

    // Update walletModel cached balance which will trigger an update for the 'labelBalance' QLabel.
    walletModel.pollBalanceChanged();
    // Check balance in send dialog
    CompareBalance(walletModel, walletModel.wallet().getBalance(), sendCoinsDialog.findChild<QLabel*>("labelBalance"));

    // Check 'UseAvailableBalance' functionality
    VerifyUseAvailableBalance(sendCoinsDialog, walletModel);

    // Send two transactions, and verify they are added to transaction list.
    TransactionTableModel* transactionTableModel = walletModel.getTransactionTableModel();
    const int rows_before_sends = transactionTableModel->rowCount({});
    const CTxDestination spend_dest = MakeTestWitnessDestination();
    uint256 txid1 = SendCoins(*wallet.get(), sendCoinsDialog, spend_dest, 5 * COIN, /*rbf=*/false);
    uint256 txid2 = SendCoins(*wallet.get(), sendCoinsDialog, spend_dest, 10 * COIN, /*rbf=*/true);
    // Transaction table model updates on a QueuedConnection; wait until both txs appear.
    for (int i = 0; i < 100; ++i) {
        qApp->processEvents(QEventLoop::AllEvents, 10);
        if (transactionTableModel->rowCount({}) >= rows_before_sends + 2) break;
        QTest::qWait(20);
    }
    QCOMPARE(transactionTableModel->rowCount({}), rows_before_sends + 2);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid2).isValid());

    // Call bumpfee. Test disabled, canceled, enabled, then failing cases.
    BumpFee(transactionView, txid1, /*expectDisabled=*/true, /*expectError=*/"not BIP 125 replaceable", /*cancel=*/false);
    BumpFee(transactionView, txid2, /*expectDisabled=*/false, /*expectError=*/{}, /*cancel=*/true);
    BumpFee(transactionView, txid2, /*expectDisabled=*/false, /*expectError=*/{}, /*cancel=*/false);
    BumpFee(transactionView, txid2, /*expectDisabled=*/true, /*expectError=*/"already bumped", /*cancel=*/false);

    // Check current balance on OverviewPage (dual-asset panels; no Bitcoin-era labelBalance).
    OverviewPage overviewPage(platformStyle.get());
    overviewPage.setWalletModel(&walletModel);
    walletModel.pollBalanceChanged(); // Manual balance polling update
    QLabel* overview_oru_confirmed = overviewPage.findChild<QLabel*>("oruConfirmedValue");
    QVERIFY(overview_oru_confirmed);
    // Overview Confirmed is min_depth>=1 (oru_balance). getBalance() is min_depth=0 and
    // includes 0-conf change after the sends above — do not compare those.
    const auto overview_bal = walletModel.wallet().getBalances();
    CompareBalance(walletModel, overview_bal.oru_balance + overview_bal.watch_only_balance, overview_oru_confirmed);

    // Check Request Payment button
    ReceiveCoinsDialog receiveCoinsDialog(platformStyle.get());
    receiveCoinsDialog.setModel(&walletModel);
    RecentRequestsTableModel* requestTableModel = walletModel.getRecentRequestsTableModel();

    // Label input
    QLineEdit* labelInput = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
    labelInput->setText("TEST_LABEL_1");

    // Amount input
    BitcoinAmountField* amountInput = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
    amountInput->setValue(1);

    // Message input
    QLineEdit* messageInput = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
    messageInput->setText("TEST_MESSAGE_1");
    int initialRowCount = requestTableModel->rowCount({});
    QPushButton* requestPaymentButton = receiveCoinsDialog.findChild<QPushButton*>("receiveButton");
    requestPaymentButton->click();
    QString address;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("ReceiveRequestDialog")) {
            ReceiveRequestDialog* receiveRequestDialog = qobject_cast<ReceiveRequestDialog*>(widget);
            auto* payment_header = receiveRequestDialog->findChild<QLabel*>("payment_header");
            QVERIFY(payment_header);
            QCOMPARE(payment_header->text(), QString("Payment information"));
            QCOMPARE(receiveRequestDialog->findChild<QLabel*>("uri_tag")->text(), QString("URI:"));
            QString uri = receiveRequestDialog->findChild<QLabel*>("uri_content")->text();
            QCOMPARE(uri.count("rigor:"), 2);
            QCOMPARE(uri.count("asset=oru"), 2);
            QCOMPARE(receiveRequestDialog->findChild<QLabel*>("address_tag")->text(), QString("Address (ORU):"));
            QVERIFY(address.isEmpty());
            address = receiveRequestDialog->QObject::findChild<QLabel*>("address_content")->text();
            QVERIFY(!address.isEmpty());

            QCOMPARE(uri.count("amount=0.00000001"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("amount_tag")->text(), QString("Amount:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("amount_content")->text(), QString::fromStdString("0.00000001 " + CURRENCY_UNIT));

            QCOMPARE(uri.count("label=TEST_LABEL_1"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("label_tag")->text(), QString("Label:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("label_content")->text(), QString("TEST_LABEL_1"));

            QCOMPARE(uri.count("message=TEST_MESSAGE_1"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("message_tag")->text(), QString("Message:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("message_content")->text(), QString("TEST_MESSAGE_1"));
        }
    }

    // Clear button
    QPushButton* clearButton = receiveCoinsDialog.findChild<QPushButton*>("clearButton");
    clearButton->click();
    QCOMPARE(labelInput->text(), QString(""));
    QCOMPARE(amountInput->value(), CAmount(0));
    QCOMPARE(messageInput->text(), QString(""));

    // Check addition to history
    int currentRowCount = requestTableModel->rowCount({});
    QCOMPARE(currentRowCount, initialRowCount+1);

    // Check addition to wallet
    std::vector<std::string> requests = walletModel.wallet().getAddressReceiveRequests();
    QCOMPARE(requests.size(), size_t{1});
    RecentRequestEntry entry;
    DataStream{MakeUCharSpan(requests[0])} >> entry;
    QCOMPARE(entry.nVersion, int{1});
    QCOMPARE(entry.id, int64_t{1});
    QVERIFY(entry.date.isValid());
    QCOMPARE(entry.recipient.address, address);
    QCOMPARE(entry.recipient.label, QString{"TEST_LABEL_1"});
    QCOMPARE(entry.recipient.amount, CAmount{1});
    QCOMPARE(entry.recipient.message, QString{"TEST_MESSAGE_1"});
    QCOMPARE(entry.recipient.sPaymentRequest, std::string{});
    QCOMPARE(entry.recipient.authenticatedMerchant, QString{});

    // Check Remove button
    QTableView* table = receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
    table->selectRow(currentRowCount-1);
    QPushButton* removeRequestButton = receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton");
    removeRequestButton->click();
    QCOMPARE(requestTableModel->rowCount({}), currentRowCount-1);

    // Check removal from wallet
    QCOMPARE(walletModel.wallet().getAddressReceiveRequests().size(), size_t{0});
}

void TestGUIWatchOnly(interfaces::Node& node, TestChain100Setup& test)
{
    const std::shared_ptr<CWallet>& wallet = SetupWatchOnlyWallet(node, test);

    // Create widgets and init models
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    MiniGUI mini_gui(node, platformStyle.get());
    mini_gui.initModelForWallet(node, wallet, platformStyle.get());
    WalletModel& walletModel = *mini_gui.walletModel;
    SendCoinsDialog& sendCoinsDialog = mini_gui.sendCoinsDialog;

    // Update walletModel cached balance which will trigger an update for the 'labelBalance' QLabel.
    walletModel.pollBalanceChanged();
    // Send dialog watch-only ORU line is confirmed + pending + immature (sendcoinsdialog.cpp).
    const auto wo = walletModel.wallet().getBalances();
    CompareBalance(walletModel,
                   wo.watch_only_balance + wo.unconfirmed_watch_only_balance + wo.immature_watch_only_balance,
                   sendCoinsDialog.findChild<QLabel*>("labelBalance"));

    // Set change address
    sendCoinsDialog.getCoinControl()->destChange = GetDestinationForKey(test.coinbaseKey.GetPubKey(), OutputType::BECH32);

    // Time to reject "save" PSBT dialog ('SendCoins' locks the main thread until the dialog receives the event).
    QTimer timer;
    timer.setInterval(500);
    QObject::connect(&timer, &QTimer::timeout, [&](){
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            // SendConfirmationDialog subclasses QMessageBox; do not dismiss it here or the send/PSBT flow aborts.
            if (widget->inherits("SendConfirmationDialog")) {
                continue;
            }
            if (widget->inherits("QMessageBox")) {
                QMessageBox* dialog = qobject_cast<QMessageBox*>(widget);
                QAbstractButton* button = dialog->button(QMessageBox::Discard);
                if (!button) continue;
                button->setEnabled(true);
                button->click();
                timer.stop();
                break;
            }
        }
    });
    timer.start(500);

    // Send tx and verify PSBT copied to the clipboard (presentPSBT sets clipboard before the dialog returns).
    // Watch-only wallets cannot broadcast: click "Create Unsigned" (Save), not "Send" (Yes).
    SendCoins(*wallet.get(), sendCoinsDialog, MakeTestWitnessDestination(), 5 * COIN, /*rbf=*/false, QMessageBox::Save);
    std::string psbt_string;
    for (int i = 0; i < 80; ++i) {
        qApp->processEvents(QEventLoop::AllEvents, 25);
        psbt_string = QApplication::clipboard()->text().toStdString();
        if (!psbt_string.empty()) break;
        QTest::qWait(25);
    }
    QVERIFY(!psbt_string.empty());

    // Decode psbt
    std::optional<std::vector<unsigned char>> decoded_psbt = DecodeBase64(psbt_string);
    QVERIFY(decoded_psbt);
    PartiallySignedTransaction psbt;
    std::string err;
    QVERIFY(DecodeRawPSBT(psbt, MakeByteSpan(*decoded_psbt), err));
}

const char* const kDisclaimerSource =
    "The wallet constructs the mint with a future confirmation-height safety margin so waiting to confirm does not void it.\n"
    "Destroyed ORU cannot be recovered.";

class InstalledCatalogs
{
public:
    InstalledCatalogs(const char* const* langs, int count) : ok(true)
    {
        for (int i = 0; i < count; ++i) {
            auto translator = std::make_unique<QTranslator>();
            if (!translator->load(QString::fromLatin1(langs[i]), QStringLiteral(":/translations/"))) {
                ok = false;
                failed = QString::fromLatin1(langs[i]);
                break;
            }
            QApplication::installTranslator(translator.get());
            installed.push_back(std::move(translator));
        }
    }
    ~InstalledCatalogs()
    {
        while (!installed.empty()) {
            QApplication::removeTranslator(installed.back().get());
            installed.pop_back();
        }
    }
    InstalledCatalogs(const InstalledCatalogs&) = delete;
    InstalledCatalogs& operator=(const InstalledCatalogs&) = delete;

    bool ok;
    QString failed;
    std::vector<std::unique_ptr<QTranslator>> installed;
};

void TestConvergenceClicks(interfaces::Node& node)
{
    TestChain100Setup test;
    // RFC-020: 128 ORU/block. Need ≥9 mature coinbases to burn ~1024 ORU for 1 RES.
    for (int i = 0; i < 15; ++i) {
        test.CreateAndProcessBlock({}, GetTestMiningCoinbaseScript(test.coinbaseKey));
    }
    auto wallet_loader = interfaces::MakeWalletLoader(*test.m_node.chain, *Assert(test.m_node.args));
    test.m_node.wallet_loader = wallet_loader.get();
    node.setContext(&test.m_node);

    const std::shared_ptr<CWallet>& wallet = SetupDescriptorsWallet(node, test);
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    MiniGUI mini_gui(node, platformStyle.get());
    mini_gui.initModelForWallet(node, wallet, platformStyle.get());
    WalletModel& walletModel = *mini_gui.walletModel;
    walletModel.pollBalanceChanged();

    ConvergencePage page(platformStyle.get());
    page.setClientModel(mini_gui.clientModel.get());
    page.setWalletModel(&walletModel);
    page.show();
    QVERIFY(QTest::qWaitFor([&]() {
        auto* price = page.findChild<QLabel*>("labelCurrentPrice");
        return price && price->text().contains(QStringLiteral("1024"));
    }, 4000));

    auto* price = page.findChild<QLabel*>("labelCurrentPrice");
    QVERIFY(price);
    QVERIFY2(price->text().contains(QStringLiteral("Current cost")), qPrintable(price->text()));
    QVERIFY2(price->text().contains(QStringLiteral("ORU per RES")), qPrintable(price->text()));
    QVERIFY2(!price->text().contains(QStringLiteral("price"), Qt::CaseInsensitive), qPrintable(price->text()));
    QVERIFY2(!price->text().contains(QStringLiteral("Won"), Qt::CaseInsensitive), qPrintable(price->text()));

    auto* next_cost = page.findChild<QLabel*>("labelNextEpochCost");
    QVERIFY(next_cost);
    QVERIFY2(next_cost->text().contains(QStringLiteral("From block")), qPrintable(next_cost->text()));
    QVERIFY2(next_cost->text().contains(QStringLiteral("1024.")), qPrintable(next_cost->text()));
    QVERIFY2(!next_cost->text().contains(QStringLiteral("210000")), qPrintable(next_cost->text()));
    QVERIFY2(next_cost->text().contains(QStringLiteral("cost becomes"), Qt::CaseInsensitive), qPrintable(next_cost->text()));
    QVERIFY2(!next_cost->text().contains(QStringLiteral("price"), Qt::CaseInsensitive), qPrintable(next_cost->text()));
    QVERIFY2(!next_cost->text().contains(QStringLiteral("last chance"), Qt::CaseInsensitive), qPrintable(next_cost->text()));
    QVERIFY2(!next_cost->text().contains(QStringLiteral("last issuance"), Qt::CaseInsensitive), qPrintable(next_cost->text()));

    auto* title = page.findChild<QLabel*>("labelQuadraticFeeTitle");
    QVERIFY(title);
    QCOMPARE(title->text(), QStringLiteral("RES this mint will create"));
    const int current_height = WITH_LOCK(test.m_node.chainman->GetMutex(),
        return test.m_node.chainman->ActiveChain().Height());
    const CAmount one_res_burn =
        Consensus::ResMinBurnForAtoms(Consensus::RES_COIN, wallet::ConvergenceTargetHeight(current_height));
    QVERIFY(one_res_burn > 0);
    auto* converge_amount = page.findChild<BitcoinAmountField*>("convergeAmount");
    QVERIFY(converge_amount);
    converge_amount->setValue(one_res_burn);
    qApp->processEvents(QEventLoop::AllEvents, 50);
    auto* minted = page.findChild<QLabel*>("labelQuadraticFee");
    QVERIFY(minted);
    QVERIFY2(minted->text().contains(QStringLiteral("1.00000000 RES")), qPrintable(minted->text()));

    RigorWallet::ConvergenceWizard wizard(&walletModel, mini_gui.clientModel.get());
    wizard.show();
    auto* amount = wizard.findChild<BitcoinAmountField*>("wizardAmountField");
    QVERIFY(amount);
    amount->setValue(one_res_burn);
    auto* next_btn = wizard.findChild<QPushButton*>("wizardNextButton");
    QVERIFY(next_btn);
    QTest::mouseClick(next_btn, Qt::LeftButton);
    qApp->processEvents(QEventLoop::AllEvents, 50);

    auto* review = wizard.findChild<QLabel*>("wizardReviewLabel");
    QVERIFY(review);
    QVERIFY2(review->text().contains(QStringLiteral("RES this transaction will create")), qPrintable(review->text()));
    QVERIFY2(review->text().contains(QStringLiteral("1.00000000 RES")) || review->text().contains(QStringLiteral("1 RES")),
             qPrintable(review->text()));
    QVERIFY2(review->text().contains(QStringLiteral("safety margin")), qPrintable(review->text()));
    QVERIFY2(review->text().contains(QStringLiteral("cannot be recovered")), qPrintable(review->text()));

    QTest::mouseClick(next_btn, Qt::LeftButton);
    qApp->processEvents(QEventLoop::AllEvents, 50);
    auto* confirm = wizard.findChild<QLabel*>("wizardConfirmLabel");
    QVERIFY(confirm);
    QVERIFY2(confirm->text().contains(QStringLiteral("destroys ORU")), qPrintable(confirm->text()));
    QVERIFY2(!confirm->text().contains(QStringLiteral("Won"), Qt::CaseInsensitive), qPrintable(confirm->text()));

    QTimer::singleShot(0, []() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* box = qobject_cast<QMessageBox*>(widget);
            if (!box) continue;
            QAbstractButton* button = box->button(QMessageBox::Ok);
            if (!button) button = box->button(QMessageBox::Yes);
            if (!button) continue;
            button->setEnabled(true);
            button->click();
        }
    });
    auto* finish_btn = wizard.findChild<QPushButton*>("wizardFinishButton");
    QVERIFY(finish_btn);
    QTest::mouseClick(finish_btn, Qt::LeftButton);
    for (int i = 0; i < 50; ++i) {
        qApp->processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(20);
        if (wizard.result() == QDialog::Accepted) break;
    }
    QCOMPARE(wizard.result(), int(QDialog::Accepted));
}

void TestGUI(interfaces::Node& node)
{
    // Set up wallet and chain with 105 blocks (5 mature blocks for spending).
    TestChain100Setup test;
    for (int i = 0; i < 5; ++i) {
        test.CreateAndProcessBlock({}, GetTestMiningCoinbaseScript(test.coinbaseKey));
    }
    auto wallet_loader = interfaces::MakeWalletLoader(*test.m_node.chain, *Assert(test.m_node.args));
    test.m_node.wallet_loader = wallet_loader.get();
    node.setContext(&test.m_node);

    // "Full" GUI tests, use descriptor wallet
    const std::shared_ptr<CWallet>& desc_wallet = SetupDescriptorsWallet(node, test);
    TestGUI(node, desc_wallet);

    // Descriptor watch-only wallet: unsigned PSBT via Create Unsigned.
    TestGUIWatchOnly(node, test);
}

} // namespace

void WalletTests::convergenceDisclaimerTests()
{
    const QString simplified = QString::fromUtf8(
        "钱包使用未来确认高度安全余量构造交易，避免因等待确认导致失效。\n"
        "已销毁的 ORU 无法恢复。");
    const QString traditional = QString::fromUtf8(
        "錢包使用未來確認高度安全餘量構造交易，避免因等待確認導致失效。\n"
        "已銷毀的 ORU 無法恢復。");
    const QString english = QString::fromUtf8(kDisclaimerSource);

    {
        std::unique_ptr<const PlatformStyle> style(PlatformStyle::instantiate("other"));
        ConvergencePage page(style.get());
        auto* label = page.findChild<QLabel*>("labelConvergeDisclaimer");
        QVERIFY(label != nullptr);
        QVERIFY2(label->text() == english, qPrintable(label->text()));
    }

#define EXPECT_DISCLAIMER(langs, count, expected, absent) \
    do { \
        InstalledCatalogs catalogs_((langs), (count)); \
        QVERIFY2(catalogs_.ok, qPrintable(catalogs_.failed)); \
        std::unique_ptr<const PlatformStyle> style_(PlatformStyle::instantiate("other")); \
        ConvergencePage page_(style_.get()); \
        auto* label_ = page_.findChild<QLabel*>("labelConvergeDisclaimer"); \
        QVERIFY(label_ != nullptr); \
        QVERIFY2(label_->text() == (expected), qPrintable(label_->text())); \
        QVERIFY2(!label_->text().contains((absent)), qPrintable(label_->text())); \
    } while (0)

    {
        const char* langs[] = {"zh", "zh_CN"};
        EXPECT_DISCLAIMER(langs, 2, simplified, QString::fromUtf8("已銷毀"));
    }
    {
        const char* langs[] = {"zh-Hans"};
        EXPECT_DISCLAIMER(langs, 1, simplified, QString::fromUtf8("已銷毀"));
    }
    {
        const char* langs[] = {"zh", "zh_TW"};
        EXPECT_DISCLAIMER(langs, 2, traditional, QString::fromUtf8("已销毁"));
    }
    {
        const char* langs[] = {"zh-Hant"};
        EXPECT_DISCLAIMER(langs, 1, traditional, QString::fromUtf8("已销毁"));
    }
    {
        const char* langs[] = {"zh_HK"};
        EXPECT_DISCLAIMER(langs, 1, traditional, QString::fromUtf8("已销毁"));
    }
    {
        const char* langs[] = {"de"};
        EXPECT_DISCLAIMER(langs, 1, english, QString::fromUtf8("无法恢复"));
    }
#undef EXPECT_DISCLAIMER

    const QDir translations(QStringLiteral(":/translations"));
    const QStringList packaged = translations.entryList(QDir::Files);
    QVERIFY2(packaged.contains(QStringLiteral("en")), "English catalog is not packaged");
    QVERIFY2(packaged.contains(QStringLiteral("zh_CN")), "zh_CN catalog is not packaged");
    QVERIFY2(packaged.contains(QStringLiteral("zh_TW")), "zh_TW catalog is not packaged");
    QVERIFY2(packaged.contains(QStringLiteral("zh-Hant")), "zh-Hant catalog is not packaged");
    QVERIFY2(packaged.contains(QStringLiteral("zh_HK")), "zh_HK catalog is not packaged");
    for (const QString& lang : packaged) {
        if (lang == QLatin1String("zh") || lang == QLatin1String("zh_CN") || lang == QLatin1String("zh-Hans")
            || lang == QLatin1String("zh_TW") || lang == QLatin1String("zh-Hant") || lang == QLatin1String("zh_HK")) {
            continue;
        }
        QTranslator translator;
        QVERIFY2(translator.load(lang, QStringLiteral(":/translations/")), qPrintable(lang));
        QApplication::installTranslator(&translator);
        const QString text = QCoreApplication::translate("ConvergencePage", kDisclaimerSource);
        QApplication::removeTranslator(&translator);
        QVERIFY2(!text.isEmpty(), qPrintable(lang));
        QVERIFY2(text == english, qPrintable(lang + QStringLiteral(": ") + text));
    }
}

void WalletTests::walletTests()
{
    if (QApplication::platformName() == "minimal") {
        // Minimal/offscreen plugins are useful for CI but can be unstable for complex modal wallet flows
        // (SendConfirmationDialog, ReceiveRequestDialog, PSBT dialogs) and may crash inside Qt internals.
        // Keep deterministic coverage on real platform plugins, and skip flaky minimal-plugin execution.
        QWARN("Skipping WalletTests with 'minimal' Qt platform due to dialog/event-loop instability. "
              "Run test_rigor-qt with a native platform plugin (xcb/windows/cocoa) for full wallet GUI coverage.");
        return;
    }
    TestGUI(m_node);
}

void WalletTests::convergenceClickTests()
{
    TestConvergenceClicks(m_node);
}
