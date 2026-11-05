// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/convergencepage.h>
#include <qt/forms/ui_convergencepage.h>

#include <qt/rigorwallet/widgets/convergence_educationpanel.h>
#include <qt/rigorwallet/widgets/convergence_networkpanel.h>
#include <qt/rigorwallet/widgets/warningbanner.h>
#include <qt/rigorwallet/rigortheme.h>

#include <chainparams.h>
#include <consensus/amount.h>
#include <consensus/consensus.h>
#include <consensus/res_issuance.h>
#include <wallet/mint_height_pad.h>
#include <interfaces/convergence_block_result.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>

#include <qt/bitcoinamountfield.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h> // SyncType, SynchronizationState
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <univalue.h>

#include <QDebug>
#include <QDialog>
#include <QElapsedTimer>
#include <QLabel>
#include <QMessageBox>
#include <QPalette>
#include <QFormLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QString>
#include <QTimer>
#include <QtGlobal>

#include <algorithm>
#include <string>

namespace {
void ClearHighlight(QLabel* label)
{
    if (!label) return;
    label->setStyleSheet(QString());
}

QString FormatMintedRes(CAmount res_atoms)
{
    return BitcoinUnits::formatAmountForAsset(
        ASSET_RES, res_atoms, BitcoinUnit::ORU, false, BitcoinUnits::SeparatorStyle::STANDARD, true);
}
} // namespace

ConvergencePage::ConvergencePage(const PlatformStyle* platformStyle, QWidget* parent)
    : QWidget(parent),
      ui(new Ui::ConvergencePage),
      m_platform_style(platformStyle)
{
    ui->setupUi(this);

    applyConvergenceTheme();

    connect(ui->pushButtonConverge, &QPushButton::clicked, this, &ConvergencePage::onSendConvergenceClicked);
    connect(ui->convergeAmount, &BitcoinAmountField::valueChanged, this, &ConvergencePage::updateMintedResPreview);

    // User Convergence page: price stats → amount entry → start.
    // ConvergenceStartPanel was removed; keep the .ui spend form visible.
    m_educationPanel = new RigorWallet::ConvergenceEducationPanel(this);
    m_educationPanel->setVisible(false);
    m_networkPanel = new RigorWallet::ConvergenceNetworkPanel(this);
    m_warningBanner = new RigorWallet::WarningBanner(this);

    if (ui->verticalLayoutConvergeScroll) {
        ui->verticalLayoutConvergeScroll->insertWidget(0, m_networkPanel);
        ui->verticalLayoutConvergeScroll->insertWidget(1, m_warningBanner);
        if (ui->frameConverge) {
            ui->frameConverge->setVisible(true);
            ui->verticalLayoutConvergeScroll->insertWidget(2, ui->frameConverge);
        }
        if (ui->labelConvergeDisclaimer) {
            ui->labelConvergeDisclaimer->setVisible(true);
            ui->labelConvergeDisclaimer->setText(tr(
                "The wallet constructs the mint with a future confirmation-height safety margin so waiting to confirm does not void it.\n"
                "Destroyed ORU cannot be recovered."));
            ui->labelConvergeDisclaimer->setStyleSheet(QStringLiteral("color: #FFFFFF; font-size: 13px;"));
            ui->verticalLayoutConvergeScroll->insertWidget(3, ui->labelConvergeDisclaimer);
        }
        ui->verticalLayoutConvergeScroll->addStretch(1);
        ui->verticalLayoutConvergeScroll->addWidget(m_educationPanel);
    }

    // Hide explorer-style / protocol frames from the user Convergence page.
    if (ui->frameNetwork) ui->frameNetwork->setVisible(false);
    if (ui->frameRoundTable) ui->frameRoundTable->setVisible(false);
    if (ui->frameMarket) ui->frameMarket->setVisible(false);

    m_refreshDebounce = new QTimer(this);
    m_refreshDebounce->setSingleShot(true);
    // Event-driven tip/wallet signals already coalesce; keep debounce Core-like (not sub-second thrash).
    m_refreshDebounce->setInterval(1500);
    connect(m_refreshDebounce, &QTimer::timeout, this, &ConvergencePage::refreshVisibleUi);

}

void ConvergencePage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    scheduleUiRefresh();
}

void ConvergencePage::scheduleUiRefresh()
{
    if (!isVisible()) return;
    if (!m_refreshDebounce) {
        refreshVisibleUi();
        return;
    }
    m_refreshDebounce->start();
}

void ConvergencePage::refreshVisibleUi()
{
    if (!isVisible()) return;
    QElapsedTimer page_timer;
    page_timer.start();

    // User page: only visible panels. Hidden protocol frames (Network/Round/Market/Draw)
    // stay compiled for Diagnostics but must not run sync RPC/disk on every tip.
    if (ui->frameNetwork && ui->frameNetwork->isVisible()) {
        updateContextPanels();
    }
    if (ui->frameRoundTable && ui->frameRoundTable->isVisible()) {
        updateConvergenceRoundPanels();
    }
    if (ui->frameMarket && ui->frameMarket->isVisible()) {
        updateMarketPanel();
    }
    if (ui->frameConverge && ui->frameConverge->isVisible()) {
        updateMinConvergenceDisplay();
    }
    updateConvergenceNetworkSafety();
    refreshPhaseCPanels();

    qInfo().noquote() << QStringLiteral("[ui-perf] ConvergencePage.refreshVisibleUi %1ms")
                             .arg(page_timer.elapsed());
}

ConvergencePage::~ConvergencePage()
{
    delete ui;
}

void ConvergencePage::applyConvergenceTheme()
{
    const RigorWallet::ColorPalette p = RigorWallet::RigorTheme::colors();
    const QString bg = RigorWallet::RigorTheme::colorHex(p.bgPrimary);
    const QString surface = RigorWallet::RigorTheme::colorHex(p.bgSurface);
    const QString border = RigorWallet::RigorTheme::colorHex(p.borderSubtle);
    const QString text = RigorWallet::RigorTheme::colorHex(p.textPrimary);
    const QString muted = RigorWallet::RigorTheme::colorHex(p.textMuted);
    const QString critical = RigorWallet::RigorTheme::colorHex(p.critical);

    setObjectName(QStringLiteral("ConvergencePageRoot"));
    setStyleSheet(QStringLiteral(
        "QWidget#ConvergencePageRoot {"
        "  background: %1;"
        "  color: %2;"
        "  font-family: %9;"
        "}"
        "QWidget#ConvergencePageRoot QLabel { color: %2; }"
        "QWidget#ConvergencePageRoot QLabel#labelSectionNetwork,"
        "QWidget#ConvergencePageRoot QLabel#labelSectionRoundTable,"
        "QWidget#ConvergencePageRoot QLabel#labelTipRoundSection,"
        "QWidget#ConvergencePageRoot QLabel#labelPrevRoundSection,"
        "QWidget#ConvergencePageRoot QLabel#labelSectionMarket,"
        "QWidget#ConvergencePageRoot QLabel#labelSectionConverge {"
        "  font-weight: 600; font-size: 13px; color: %2;"
        "}"
        "QWidget#ConvergencePageRoot QFrame#frameNetwork,"
        "QWidget#ConvergencePageRoot QFrame#frameRoundTable,"
        "QWidget#ConvergencePageRoot QFrame#frameTipRound,"
        "QWidget#ConvergencePageRoot QFrame#framePrevRound,"
        "QWidget#ConvergencePageRoot QFrame#frameMarket,"
        "QWidget#ConvergencePageRoot QFrame#frameConverge {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %4;"
        "  border-radius: 8px;"
        "}"
        "QWidget#ConvergencePageRoot QScrollArea { background: transparent; border: none; }"
        "QWidget#ConvergencePageRoot QWidget#scrollAreaWidgetContents { background: transparent; }"
        "QWidget#ConvergencePageRoot QLabel#labelNetPeersTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelNetHeightTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelNetSyncTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelMinConvergeTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelConvergeAmountTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelQuadraticFeeTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelTotalDeductionTitle { color: %5; }"
        "QWidget#ConvergencePageRoot QLabel#labelRoundNarrative { color: %5; font-size: 11px; }"
        "QWidget#ConvergencePageRoot QLabel#labelConvergeDisclaimer { color: #FFFFFF; font-size: 13px; }"
        "QWidget#ConvergencePageRoot QLabel#labelTipRoundHeightTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelTipRoundNTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelTipRoundYouTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelPrevRoundHeightTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelPrevRoundNTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelPrevRoundYouTitle { color: %5; }"
        "QWidget#ConvergencePageRoot QLabel#labelTipRoundError,"
        "QWidget#ConvergencePageRoot QLabel#labelPrevRoundError { color: %6; font-size: 11px; }"
        "QWidget#ConvergencePageRoot QLabel#labelMarketResTotalTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelMarketWindowTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelMarketMintCostTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelMarketTipTitle,"
        "QWidget#ConvergencePageRoot QLabel#labelMarketLastResTitle { color: %5; }"
        "QWidget#ConvergencePageRoot QLabel#labelConvergeUnavailable { color: %2; padding: 12px; background: %3; border: 1px solid %4; border-radius: 8px; }"
        "QWidget#ConvergencePageRoot QPushButton#pushButtonConverge {"
        "  background-color: %7;"
        "  color: #FFFFFF;"
        "  font-weight: 600;"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 8px 16px;"
        "}"
        "QWidget#ConvergencePageRoot QPushButton#pushButtonConverge:hover { background-color: %8; }"
        "QWidget#ConvergencePageRoot QPushButton#pushButtonConverge:disabled { background-color: %4; color: %5; }"
        ).arg(bg, text, surface, border, muted, critical,
              RigorWallet::RigorTheme::colorHex(p.accent),
              RigorWallet::RigorTheme::colorHex(p.accentHover),
              GUIUtil::UiFontFamilyCss()));
}

void ConvergencePage::updateConvergenceAvailability()
{
    const bool can_convergence = walletModel && !walletModel->wallet().privateKeysDisabled();
    ui->scrollAreaConverge->setVisible(can_convergence);
    ui->labelConvergeUnavailable->setVisible(!can_convergence);
}

void ConvergencePage::setClientModel(ClientModel* model)
{
    clientModel = model;
    if (model) {
        connect(model, &ClientModel::numBlocksChanged, this, [this](int, const QDateTime&, double, SyncType, SynchronizationState) {
            scheduleUiRefresh();
        });
        connect(model, &ClientModel::numConnectionsChanged, this, [this](int) {
            if (!isVisible()) return;
            // Hidden protocol frames must not run sync RPC on connection flaps.
            if (ui->frameNetwork && ui->frameNetwork->isVisible()) {
                updateContextPanels();
            }
            updateConvergenceNetworkSafety();
        });
        if (model->getOptionsModel()) {
            connect(model->getOptionsModel(), &OptionsModel::useEmbeddedMonospacedFontChanged, this, &ConvergencePage::setMonospacedConvergenceFont);
            setMonospacedConvergenceFont(model->getOptionsModel()->getUseEmbeddedMonospacedFont());
        }
    }
    if (m_networkPanel) m_networkPanel->setClientModel(model);
    scheduleUiRefresh();
}

void ConvergencePage::setWalletModel(WalletModel* model)
{
    walletModel = model;
    updateConvergenceAvailability();
    if (model && model->getOptionsModel()) {
        // Page amount field is hidden; wizard locks to ORU. Keep field on ORU if ever re-enabled.
        ui->convergeAmount->setDisplayUnit(BitcoinUnit::ORU);
        connect(model, &WalletModel::walletStateRevisionChanged, this, [this](quint64) {
            scheduleUiRefresh();
        });
        connect(model, &WalletModel::balanceSnapshotStaleChanged, this, [this](bool) {
            scheduleUiRefresh();
        });
        connect(model, &WalletModel::notifyWatchonlyChanged, this, [this](bool) {
            updateConvergenceAvailability();
            if (isVisible()) {
                updateMintedResPreview();
                scheduleUiRefresh();
            }
        });
        updateMintedResPreview();
    }
    scheduleUiRefresh();
}

void ConvergencePage::setMonospacedConvergenceFont(bool use_embedded_font)
{
    QFont f = GUIUtil::fixedPitchFont(use_embedded_font);
    f.setWeight(QFont::Bold);
    ui->labelMinConverge->setFont(f);
    ui->labelQuadraticFee->setFont(f);
    ui->labelTotalDeduction->setFont(f);
    ui->labelNetHeight->setFont(f);
    ui->labelMarketResTotal->setFont(f);
    ui->labelMarketWindow->setFont(f);
    ui->labelMarketMintCost->setFont(f);
    ui->labelMarketTip->setFont(f);
    ui->labelMarketLastRes->setFont(f);
    ui->labelTipRoundHeight->setFont(f);
    ui->labelTipRoundN->setFont(f);
    ui->labelPrevRoundHeight->setFont(f);
    ui->labelPrevRoundN->setFont(f);
}

void ConvergencePage::updateContextPanels()
{
    if (!clientModel) {
        ui->labelNetPeers->setText(tr("—"));
        ui->labelNetHeight->setText(tr("—"));
        ui->labelNetSync->setText(tr("—"));
        return;
    }

    const int peers = clientModel->getNumConnections();
    ui->labelNetPeers->setText(QString::number(peers));

    const int tip = clientModel->getNumBlocks();
    ui->labelNetHeight->setText(QString::number(tip));

    const bool ibd = clientModel->node().isInitialBlockDownload();
    const int headers = clientModel->getHeaderTipHeight();
    QString sync_txt;
    if (ibd) {
        sync_txt = tr("Initial block download…");
    } else if (headers >= 0 && tip != headers) {
        sync_txt = tr("Synchronizing (%1 / %2)").arg(tip).arg(headers);
    } else {
        sync_txt = tr("Synced");
    }
    ui->labelNetSync->setText(sync_txt);
}

void ConvergencePage::updateMarketPanel()
{
    auto dash = [this]() {
        ui->labelMarketResTotal->setText(tr("—"));
        ui->labelMarketWindow->setText(tr("—"));
        ui->labelMarketMintCost->setText(tr("—"));
        ui->labelMarketTip->setText(tr("—"));
        ui->labelMarketLastRes->setText(tr("—"));
    };
    if (!clientModel) {
        dash();
        return;
    }
    try {
        UniValue params(UniValue::VARR);
        UniValue r = clientModel->node().executeRpc("getconvergencestatus", params, "/");
        if (!r.isObject()) {
            dash();
            return;
        }
        ui->labelMarketResTotal->setText(QString::fromStdString(r["res_total"].getValStr()));
        if (r.exists("confirm_height")) {
            ui->labelMarketWindow->setText(QString::number(r["confirm_height"].getInt<int>()));
        } else {
            ui->labelMarketWindow->setText(tr("—"));
        }
        if (r.exists("destroy_cost")) {
            // Display destroy_cost(h) only. Do not feed this value back as the burn B.
            ui->labelMarketMintCost->setText(
                BitcoinUnits::formatWithUnit(BitcoinUnit::ORU, r["destroy_cost"].getInt<int64_t>())
                + QStringLiteral(" / RES"));
        } else {
            ui->labelMarketMintCost->setText(tr("—"));
        }
        ui->labelMarketTip->setText(QString::number(r["tip_height"].getInt<int>()));
        ui->labelMarketLastRes->setText(QString::number(r["last_res_height"].getInt<int>()));
    } catch (...) {
        dash();
    }
}

void ConvergencePage::updateConvergenceRoundPanels()
{
    if (!ui->frameRoundTable) return;

    auto dash_tip = [this]() {
        ui->labelTipRoundHeight->setText(tr("—"));
        ui->labelTipRoundN->setText(tr("—"));
        ui->labelTipRoundYou->setText(tr("—"));
        ClearHighlight(ui->labelTipRoundYou);
        ui->labelTipRoundError->clear();
        ui->labelTipRoundError->setVisible(false);
    };
    auto dash_prev = [this]() {
        ui->labelPrevRoundHeight->setText(tr("—"));
        ui->labelPrevRoundN->setText(tr("—"));
        ui->labelPrevRoundYou->setText(tr("—"));
        ClearHighlight(ui->labelPrevRoundYou);
        ui->labelPrevRoundError->clear();
        ui->labelPrevRoundError->setVisible(false);
    };

    if (!clientModel) {
        dash_tip();
        dash_prev();
        ui->framePrevRound->setVisible(false);
        return;
    }

    const int tip = clientModel->getNumBlocks();

    auto fetch_at = [&](int height, interfaces::ConvergenceBlockResult& out) -> bool {
        if (walletModel) {
            return walletModel->tryGetConvergenceBlockResult(height, out);
        }
        return clientModel->getConvergenceBlockResult(height, {}, out);
    };

    auto apply_row = [&](int height, const interfaces::ConvergenceBlockResult& r, bool got,
                         QLabel* err_label,
                         QLabel* height_l,
                         QLabel* n_l,
                         QLabel* you_l) {
        err_label->clear();
        err_label->setVisible(false);
        if (!got || !r.ok) {
            height_l->setText(height >= 0 ? QString::number(height) : tr("—"));
            n_l->setText(tr("—"));
            you_l->setText(tr("—"));
            ClearHighlight(you_l);
            const QString msg = r.error_message.empty()
                ? tr("Unable to load convergence data for this block.")
                : QString::fromStdString(r.error_message);
            err_label->setText(msg);
            err_label->setVisible(true);
            return;
        }
        height_l->setText(QString::number(r.height));
        n_l->setText(QString::number(r.convergence_tx_count));
        if (walletModel) {
            if (r.is_mine) {
                you_l->setText(tr("Yes — your convergence transaction is in this block."));
                you_l->setStyleSheet(QStringLiteral("QLabel { color: #8CD892; font-weight: 600; }"));
            } else {
                ClearHighlight(you_l);
                you_l->setText(tr("No"));
            }
        } else {
            ClearHighlight(you_l);
            you_l->setText(tr("— (no wallet)"));
        }
    };

    interfaces::ConvergenceBlockResult tip_r;
    const bool tip_got = fetch_at(tip, tip_r);
    apply_row(tip, tip_r, tip_got,
        ui->labelTipRoundError,
        ui->labelTipRoundHeight,
        ui->labelTipRoundN,
        ui->labelTipRoundYou);

    if (tip < 1) {
        dash_prev();
        ui->framePrevRound->setVisible(false);
        return;
    }
    ui->framePrevRound->setVisible(true);
    interfaces::ConvergenceBlockResult prev_r;
    const bool prev_got = fetch_at(tip - 1, prev_r);
    apply_row(tip - 1, prev_r, prev_got,
        ui->labelPrevRoundError,
        ui->labelPrevRoundHeight,
        ui->labelPrevRoundN,
        ui->labelPrevRoundYou);
}

void ConvergencePage::loadMintPreviewParams()
{
    m_mint_preview_ok = false;
    m_mint_authorized = false;
    m_mint_destroy_cost_height = 0;
    if (!clientModel) {
        m_mint_preview_tip = -2;
        return;
    }
    const int tip = clientModel->getNumBlocks();
    try {
        UniValue params(UniValue::VARR);
        UniValue r = clientModel->node().executeRpc("getconvergencestatus", params, "/");
        if (!r.isObject()) {
            return;
        }
        if (!r.exists("confirm_height")) {
            return;
        }
        const int confirm_height = r["confirm_height"].getInt<int>();
        m_mint_authorized = r.exists("issuance_authorized")
            ? r["issuance_authorized"].get_bool()
            : (confirm_height >= 0);
        // Wallet construction: price at current tip + CONVERGENCE_HEIGHT_PAD.
        m_mint_destroy_cost_height = wallet::ConvergenceTargetHeight(tip);
        m_mint_preview_tip = tip;
        m_mint_preview_ok = true;
    } catch (...) {
        m_mint_preview_tip = -2;
    }
}

void ConvergencePage::updateMintedResPreview()
{
    if (!walletModel || !walletModel->getOptionsModel()) {
        ui->labelQuadraticFee->setText(tr("—"));
        ui->labelTotalDeduction->setText(tr("—"));
        return;
    }
    bool ok = false;
    const CAmount amt = ui->convergeAmount->value(&ok);
    if (!ok || amt <= 0) {
        ui->labelQuadraticFee->setText(tr("—"));
        ui->labelTotalDeduction->setText(tr("—"));
        return;
    }
    const BitcoinUnit unit = walletModel->getOptionsModel()->getDisplayUnit();
    ui->labelTotalDeduction->setText(BitcoinUnits::formatWithUnit(unit, amt));

    const int tip = clientModel ? clientModel->getNumBlocks() : -2;
    if (!m_mint_preview_ok || m_mint_preview_tip != tip) {
        loadMintPreviewParams();
    }
    if (!m_mint_preview_ok) {
        ui->labelQuadraticFee->setText(tr("—"));
        return;
    }
    if (!m_mint_authorized || m_mint_destroy_cost_height < 0) {
        // RFC-020 has no mint horizon. A missing price is a node/sync failure, not "ended".
        ui->labelQuadraticFee->setText(tr("Unable to price this mint"));
        return;
    }
    const CAmount minted = Consensus::ResMintAtomsFromBurn(amt, m_mint_destroy_cost_height);
    ui->labelQuadraticFee->setText(FormatMintedRes(minted));
}

void ConvergencePage::updateConvergenceNetworkSafety()
{
    updateRecoveryWarnings();
}

void ConvergencePage::refreshPhaseCPanels()
{
    auto refresh_if_visible = [](QWidget* w, const auto& fn) {
        if (w && w->isVisible()) fn();
    };
    refresh_if_visible(m_networkPanel, [this] { m_networkPanel->refresh(); });
    // startPanel already refreshed in updateConvergenceNetworkSafety when visible
    // updateRecoveryWarnings already ran from updateConvergenceNetworkSafety in refreshVisibleUi.
}

void ConvergencePage::updateRecoveryWarnings()
{
    if (!m_warningBanner) return;

    QString msg;
    RigorWallet::WarningLevel level = RigorWallet::WarningLevel::Info;

    if (!clientModel || !walletModel) {
        m_warningBanner->hideBanner();
        return;
    }

    const int peers = clientModel->getNumConnections();
    if (peers <= 0 && !Params().IsMockableChain()) {
        msg = tr("Wallet is offline. Connect to the RIGOR network before starting Convergence.");
        level = RigorWallet::WarningLevel::Critical;
    } else if (clientModel->node().isInitialBlockDownload()) {
        msg = tr("Syncing — wait until the wallet finishes catching up.");
        level = RigorWallet::WarningLevel::Warning;
    } else if (walletModel->wallet().privateKeysDisabled()) {
        msg = tr("Wallet locked for spending — unlock to start Convergence.");
        level = RigorWallet::WarningLevel::Warning;
    }
    // Amount entry / insufficient-ORU validation lives in frameConverge.

    if (msg.isEmpty()) {
        m_warningBanner->hideBanner();
    } else {
        m_warningBanner->showBanner(level, msg);
    }
}

void ConvergencePage::updateMinConvergenceDisplay()
{
    if (!clientModel) {
        ui->labelMinConverge->setText(tr("—"));
        return;
    }
    CAmount min_amt = Consensus::ResMinMintBurnAtoms(0);
    int confirm_height = -1;
    try {
        UniValue params(UniValue::VARR);
        UniValue r = clientModel->node().executeRpc("getconvergencestatus", params, "/");
        if (r.isObject() && r.exists("confirm_height")) {
            confirm_height = r["confirm_height"].getInt<int>();
        }
        if (confirm_height >= 0) {
            const int current_height = clientModel->getNumBlocks();
            min_amt = Consensus::ResMinMintBurnAtoms(wallet::ConvergenceTargetHeight(current_height));
        }
    } catch (...) {
    }
    if (confirm_height >= 0) {
        ui->labelMinConverge->setText(
            tr("%1 (valid for %2 blocks)")
                .arg(BitcoinUnits::formatWithUnit(BitcoinUnit::ORU, min_amt))
                .arg(wallet::CONVERGENCE_HEIGHT_PAD));
    } else {
        ui->labelMinConverge->setText(BitcoinUnits::formatWithUnit(BitcoinUnit::ORU, min_amt));
    }
    updateMintedResPreview();
}

void ConvergencePage::onSendConvergenceClicked()
{
    if (!walletModel || !walletModel->getOptionsModel() || !clientModel) {
        return;
    }
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        return;
    }
    bool ok = false;
    const CAmount amt = ui->convergeAmount->value(&ok);
    if (!ok || amt <= 0) {
        QMessageBox::warning(this, tr("Convergence"),
            tr("Enter a valid convergence amount. Reason: amount must be positive. Suggestion: enter a value above zero."));
        return;
    }
    loadMintPreviewParams();
    if (!m_mint_preview_ok) {
        QMessageBox::warning(this, tr("Convergence"),
            tr("Unable to read the current mint cost from the node. Try again after the wallet is synced."));
        return;
    }
    if (!m_mint_authorized || m_mint_destroy_cost_height < 0) {
        QMessageBox::warning(this, tr("Convergence"),
            tr("Unable to read the confirming-height mint cost from the node. Try again after the wallet is synced."));
        return;
    }
    const CAmount min_amt = Consensus::ResMinMintBurnAtoms(m_mint_destroy_cost_height);
    if (amt < min_amt) {
        QMessageBox::warning(this, tr("Convergence"),
            tr("Amount is below the ORU that mints 1 RES atom (%1). Increase the amount.")
                .arg(BitcoinUnits::formatWithUnit(BitcoinUnit::ORU, min_amt)));
        return;
    }

    const auto balances = walletModel->getCachedBalance();
    const CAmount oru_avail = balances.oru_balance + balances.oru_unconfirmed_balance;
    if (amt > oru_avail) {
        QMessageBox::warning(this, tr("Convergence"),
            tr("Insufficient ORU available. Reason: the convergence amount exceeds your spendable and pending ORU. Suggestion: lower the amount or fund the wallet."));
        return;
    }

    const CAmount large_threshold = std::max(min_amt * 10, oru_avail / 2);
    if (oru_avail > 0 && amt >= large_threshold) {
        const auto r = QMessageBox::question(this, tr("Large convergence amount"),
            tr("You are about to convergence a large amount of ORU relative to your balance. Continue?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }

    const CAmount minted = Consensus::ResMintAtomsFromBurn(amt, m_mint_destroy_cost_height);
    const auto r2 = QMessageBox::question(this, tr("Confirm Convergence"),
        tr("This mint will create %1.\n"
           "Convergence uses ORU permanently. This cannot be undone after broadcast.\n\nContinue?")
            .arg(FormatMintedRes(minted)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r2 != QMessageBox::Yes) return;

    ui->pushButtonConverge->setEnabled(false);
    ConvergencePage* self_ptr = this;
    QTimer::singleShot(2700, this, [this, self_ptr]() {
        if (self_ptr && ui->pushButtonConverge) ui->pushButtonConverge->setEnabled(true);
    });

    const auto ret = walletModel->sendConvergenceOru(amt);
    if (ret.status == WalletModel::OK) {
        QMessageBox::information(this, tr("Convergence"),
            tr("Convergence submitted."));
        ui->convergeAmount->clear();
        updateMintedResPreview();
        updateConvergenceRoundPanels();
        refreshPhaseCPanels();
        updateRecoveryWarnings();
    }
}
