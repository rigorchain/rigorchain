// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/rigor-config.h>
#endif

#include <qt/splashscreen.h>

#include <clientversion.h>
#include <qt/rigorwallet/walletidentity.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qt/guiutil.h>
#include <qt/networkstyle.h>
#include <qt/walletmodel.h>
#include <util/system.h>
#include <util/translation.h>

#include <functional>

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFont>
#include <QPainter>
#include <QRadialGradient>
#include <QScreen>

namespace {
/** Blue-black ballpoint on the parchment splash. The logo pixmap is not recolored. */
const QColor SPLASH_GOLD(0x26, 0x39, 0x4B);
const QColor SPLASH_BG(0xE3, 0xD1, 0xB8);
const QColor SPLASH_TEXT(0x26, 0x39, 0x4B);
const QColor SPLASH_MUTED(0x52, 0x64, 0x77);

QFont SplashScreenStatusFont()
{
    QFont f = QApplication::font();
    if (f.pointSizeF() > 0.0) {
        f.setPointSizeF(f.pointSizeF() * 1.5);
    } else if (f.pixelSize() > 0) {
        f.setPixelSize(static_cast<int>(f.pixelSize() * 1.5));
    } else {
        f.setPointSizeF(14.0);
    }
    f.setWeight(QFont::Normal);
    return f;
}

/** Production splash: logo + Rigor Wallet product copy (no mining-reward / lottery / burn wording). */
QPixmap BuildProductionSplash(const NetworkStyle* networkStyle, qreal devicePixelRatio)
{
    const int W = 720;
    const int H = 480;
    QPixmap pixmap(static_cast<int>(W * devicePixelRatio), static_cast<int>(H * devicePixelRatio));
    pixmap.setDevicePixelRatio(devicePixelRatio);
    pixmap.fill(SPLASH_BG);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    p.setPen(QPen(QColor(0x8A, 0x7E, 0x6C), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRect(0, 0, W - 1, H - 1));

    // Official app icon
    const QSize iconPx(280, 280);
    QPixmap icon = networkStyle->getAppIcon().pixmap(QSize(
        static_cast<int>(iconPx.width() * devicePixelRatio),
        static_cast<int>(iconPx.height() * devicePixelRatio)));
    icon.setDevicePixelRatio(devicePixelRatio);
    const int iconX = 36;
    const int iconY = (H - iconPx.height()) / 2 - 10;
    p.drawPixmap(iconX, iconY, iconPx.width(), iconPx.height(), icon);

    const int textX = 340;
    int y = 70;

    QFont titleFont = QApplication::font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(28.0);
    p.setFont(titleFont);
    p.setPen(SPLASH_GOLD);
    p.drawText(textX, y, RigorWallet::WalletIdentity::brand().productName);
    y += 42;

    QFont body = QApplication::font();
    body.setPointSizeF(11.5);
    p.setFont(body);
    p.setPen(SPLASH_TEXT);

    auto drawBlock = [&](const QString& heading, const QString& bodyText) {
        QFont h = body;
        h.setBold(true);
        h.setPointSizeF(12.5);
        p.setFont(h);
        p.setPen(SPLASH_GOLD);
        p.drawText(textX, y, heading);
        y += 20;
        p.setFont(body);
        p.setPen(SPLASH_MUTED);
        QRect r(textX, y, W - textX - 36, 48);
        p.drawText(r, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, bodyText);
        y += 52;
    };

    drawBlock(QStringLiteral("ORU"),
              QStringLiteral("Proof-of-Work Security Asset"));
    drawBlock(QStringLiteral("RES"),
              QStringLiteral("Independent Scarce Resource — minted only through Convergence"));

    p.setFont(body);
    p.setPen(SPLASH_GOLD);
    QRect compliance(textX, y, W - textX - 36, 36);
    p.drawText(compliance, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               QStringLiteral("The script is the compliance layer."));
    y += 40;

    // Version + dual MIT copyright (Bitcoin Core provenance + RIGOR). Use QChar to avoid
    // UTF-8 mojibake (e.g. "Â©") when the source encoding is misinterpreted.
    const QString copyMark = QString(QChar(0x00A9));
    const QString versionStr = QString::fromStdString(FormatFullVersion());

    QFont verFont = body;
    verFont.setBold(true);
    verFont.setPointSizeF(11.0);
    p.setFont(verFont);
    p.setPen(SPLASH_GOLD);
    p.drawText(textX, y, QStringLiteral("Version %1").arg(versionStr));
    y += 20;

    QFont small = body;
    small.setPointSizeF(9.0);
    p.setFont(small);
    p.setPen(SPLASH_MUTED);
    p.drawText(textX, y, QStringLiteral("%1 2009-2026 The Bitcoin Core developers").arg(copyMark));
    y += 16;
    p.drawText(textX, y, QStringLiteral("%1 2026- The RIGOR developers").arg(copyMark));
    y += 16;
    p.drawText(textX, y, QStringLiteral("License: MIT"));

    const QString& titleAddText = networkStyle->getTitleAddText();
    if (!titleAddText.isEmpty()) {
        QFont net = small;
        net.setBold(true);
        p.setFont(net);
        p.setPen(SPLASH_GOLD);
        p.drawText(W - 120, 28, titleAddText);
    }

    p.end();
    return pixmap;
}
} // namespace

SplashScreen::SplashScreen(const NetworkStyle* networkStyle)
    : QWidget()
{
    float devicePixelRatio = 1.0;
    devicePixelRatio = static_cast<QGuiApplication*>(QCoreApplication::instance())->devicePixelRatio();

    QString titleText = RigorWallet::WalletIdentity::brand().productName;
    const QString& titleAddText = networkStyle->getTitleAddText();

    // Always compose the production product splash (logo + ORU/RES + compliance).
    // Do not use legacy Bitcoin-era splash art as the primary surface.
    pixmap = BuildProductionSplash(networkStyle, devicePixelRatio);

    setWindowTitle(titleText + (titleAddText.isEmpty() ? QString() : QStringLiteral(" ") + titleAddText));

    QRect r(QPoint(), QSize(pixmap.size().width() / devicePixelRatio, pixmap.size().height() / devicePixelRatio));
    resize(r.size());
    setFixedSize(r.size());
    setWindowFlag(Qt::WindowMaximizeButtonHint, false);
    move(QGuiApplication::primaryScreen()->geometry().center() - r.center());

    installEventFilter(this);
    GUIUtil::handleCloseWindowShortcut(this);
}

SplashScreen::~SplashScreen()
{
    if (m_node) unsubscribeFromCoreSignals();
}

void SplashScreen::setNode(interfaces::Node& node)
{
    assert(!m_node);
    m_node = &node;
    subscribeToCoreSignals();
    if (m_shutdown) m_node->startShutdown();
}

void SplashScreen::shutdown()
{
    m_shutdown = true;
    if (m_node) m_node->startShutdown();
}

void SplashScreen::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::WindowStateChange && isMaximized()) {
        showNormal();
    }
    QWidget::changeEvent(event);
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if (keyEvent->key() == Qt::Key_Q) {
            shutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    bool invoked = QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, SPLASH_GOLD));
    assert(invoked);
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? SplashScreen::tr("(press q to shutdown and continue later)").toStdString()
                                : SplashScreen::tr("press q to shutdown").toStdString()) +
            strprintf("\n%d", nProgress) + "%");
}

void SplashScreen::subscribeToCoreSignals()
{
    m_handler_init_message = m_node->handleInitMessage(std::bind(InitMessage, this, std::placeholders::_1));
    m_handler_show_progress = m_node->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_handler_init_wallet = m_node->handleInitWallet([this]() { handleLoadWallet(); });
}

void SplashScreen::handleLoadWallet()
{
#ifdef ENABLE_WALLET
    if (!WalletModel::isWalletEnabled()) return;
    m_handler_load_wallet = m_node->walletLoader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, false)));
        m_connected_wallets.emplace_back(std::move(wallet));
    });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    for (const auto& handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setFont(SplashScreenStatusFont());
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    shutdown();
    event->ignore();
}
