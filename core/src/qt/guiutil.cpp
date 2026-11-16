// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/guiutil.h>

#include <qt/bitcoinaddressvalidator.h>
#include <qt/bitcoinunits.h>
#include <qt/platformstyle.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/sendcoinsrecipient.h>

#include <wallet/asset_domain.h>

#include <base58.h>
#include <chainparams.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <protocol.h>
#include <script/script.h>
#include <script/standard.h>
#include <util/exception.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <util/system.h>
#include <util/time.h>

#ifdef WIN32
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#endif

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDoubleValidator>
#include <functional>

#include <QFileDialog>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QPalette>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLatin1String>
#include <QLineEdit>
#include <QList>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPluginLoader>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSize>
#include <QStandardPaths>
#include <QString>
#include <QTextDocument> // for Qt::mightBeRichText
#include <QThread>
#include <QUrlQuery>
#include <QtGlobal>

#include <cassert>
#include <chrono>
#include <exception>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#if defined(Q_OS_MACOS)

#include <QProcess>

void ForceActivation();
#endif

using namespace std::chrono_literals;

namespace GUIUtil {

QString dateTimeStr(const QDateTime &date)
{
    return QLocale::system().toString(date.date(), QLocale::ShortFormat) + QString(" ") + date.toString("hh:mm");
}

QString dateTimeStr(qint64 nTime)
{
    return dateTimeStr(QDateTime::fromSecsSinceEpoch(nTime));
}

QFont fixedPitchFont(bool use_embedded_font)
{
    if (use_embedded_font) {
        return {"Roboto Mono"};
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
}

// Just some dummy data to generate a convincing random-looking (but consistent) address
static const uint8_t dummydata[] = {0xeb,0x15,0x23,0x1d,0xfc,0xeb,0x60,0x92,0x58,0x86,0xb6,0x7d,0x06,0x52,0x99,0x92,0x59,0x15,0xae,0xb1,0x72,0xc0,0x66,0x47};

/** Example native address for UI placeholders (adapts to chain Bech32 HRP). */
static std::string DummyNativeAddress(AssetType asset)
{
    if (asset == ASSET_RES) {
        unsigned char data[32]{};
        for (size_t i = 0; i < sizeof(data); ++i) {
            data[i] = dummydata[i % sizeof(dummydata)] ^ static_cast<uint8_t>(i * 17);
        }
        return EncodeDestination(WitnessV1Res{XOnlyPubKey{Span<const unsigned char>{data, 32}}});
    }
    uint160 id;
    std::copy(dummydata, dummydata + 20, id.begin());
    return EncodeDestination(WitnessV0KeyHash{id});
}

void updateAddressWidgetPlaceholder(QValidatedLineEdit* widget, AssetType asset)
{
    if (!widget) return;
    const QString example = QString::fromStdString(DummyNativeAddress(asset));
    if (asset == ASSET_RES) {
        widget->setPlaceholderText(QObject::tr("Enter a RES address (e.g. %1)").arg(example));
    } else {
        widget->setPlaceholderText(QObject::tr("Enter an ORU address (e.g. %1)").arg(example));
    }
}

void setupAddressWidget(QValidatedLineEdit *widget, QWidget *parent)
{
    parent->setFocusProxy(widget);

    widget->setFont(fixedPitchFont());
    // Default to ORU / Bech32; Send tab updates via updateAddressWidgetPlaceholder when Asset changes.
    updateAddressWidgetPlaceholder(widget, ASSET_ORU);
    widget->setValidator(new BitcoinAddressEntryValidator(parent));
    widget->setCheckValidator(new BitcoinAddressCheckValidator(parent));
}

void AddButtonShortcut(QAbstractButton* button, const QKeySequence& shortcut)
{
    QObject::connect(new QShortcut(shortcut, button), &QShortcut::activated, [button]() { button->animateClick(); });
}

bool parseBitcoinURI(const QUrl &uri, SendCoinsRecipient *out)
{
    // return if URI is not valid or is no rigor: URI
    if(!uri.isValid() || uri.scheme() != QString("rigor"))
        return false;

    SendCoinsRecipient rv;
    rv.address = uri.path();
    // Trim any following forward slash which may have been added by the OS
    if (rv.address.endsWith("/")) {
        rv.address.truncate(rv.address.length() - 1);
    }
    rv.amount = 0;

    QString amount_param;
    bool have_asset = false;
    AssetType uri_asset = ASSET_ORU;

    QUrlQuery uriQuery(uri);
    const QList<QPair<QString, QString>> items = uriQuery.queryItems();
    for (QList<QPair<QString, QString>>::const_iterator i = items.begin(); i != items.end(); ++i)
    {
        QString key = i->first;
        bool unknown_req = false;
        if (key.startsWith(QStringLiteral("req-")))
        {
            key.remove(0, 4);
            unknown_req = true;
        }

        if (key == QStringLiteral("label"))
        {
            rv.label = i->second;
            unknown_req = false;
        }
        else if (key == QStringLiteral("message"))
        {
            rv.message = i->second;
            unknown_req = false;
        }
        else if (key == QStringLiteral("amount"))
        {
            amount_param = i->second;
            unknown_req = false;
        }
        else if (key == QStringLiteral("asset"))
        {
            const QString v = i->second.trimmed().toLower();
            AssetType parsed = ASSET_ORU;
            if (v == QStringLiteral("oru")) {
                parsed = ASSET_ORU;
            } else if (v == QStringLiteral("res")) {
                parsed = ASSET_RES;
            } else {
                return false;
            }
            if (have_asset && uri_asset != parsed) {
                return false;
            }
            uri_asset = parsed;
            have_asset = true;
            unknown_req = false;
        }

        if (unknown_req)
            return false;
    }

    // W1.1D FREEZE INVARIANT (L6 GUI URI parser, F-A-09): rigor: URIs MUST carry an
    // explicit asset= query parameter. The GUI URI layer is the strictest UX surface
    // (manual user paste is the highest-risk human-error vector) and intentionally
    // forbids the ORU-default fallback that RPC callers may use. Do NOT add a default
    // here without a new constitutional review.
    if (!have_asset) {
        return false;
    }

    std::string dec_err;
    const CTxDestination uri_dest = DecodeDestination(rv.address.toStdString(), dec_err, nullptr);
    if (!IsValidDestination(uri_dest)) {
        return false;
    }
    const CScript uri_spk = GetScriptForDestination(uri_dest);
    if (!wallet::WalletStrictSendDestinationOk(uri_spk, uri_asset)) {
        return false;
    }

    if (!amount_param.isEmpty())
    {
        if (uri_asset == ASSET_RES) {
            if (!BitcoinUnits::parseRes(amount_param, &rv.amount)) {
                return false;
            }
        } else {
            if (!BitcoinUnits::parse(BitcoinUnit::ORU, amount_param, &rv.amount)) {
                return false;
            }
        }
    }

    rv.asset = uri_asset;

    if(out)
    {
        *out = rv;
    }
    return true;
}

bool parseBitcoinURI(QString uri, SendCoinsRecipient *out)
{
    QUrl uriInstance(uri);
    return parseBitcoinURI(uriInstance, out);
}

QString formatBitcoinURI(const SendCoinsRecipient &info)
{
    bool bech_32 = info.address.startsWith(QString::fromStdString(Params().Bech32HRP() + "1"));

    QString ret = QString("rigor:%1").arg(bech_32 ? info.address.toUpper() : info.address);

    AssetType asset_dom = ASSET_ORU;
    {
        std::string dec_err;
        const CTxDestination d = DecodeDestination(info.address.toStdString(), dec_err, nullptr);
        if (IsValidDestination(d)) {
            asset_dom = AssetTypeFromScriptPubKey(GetScriptForDestination(d));
        } else if (info.asset.has_value()) {
            asset_dom = *info.asset;
        }
    }

    ret += QStringLiteral("?asset=%1").arg(asset_dom == ASSET_RES ? QStringLiteral("res") : QStringLiteral("oru"));

    if (info.amount)
    {
        if (asset_dom == ASSET_RES) {
            ret += QStringLiteral("&amount=%1").arg(BitcoinUnits::formatRes(info.amount, false, BitcoinUnits::SeparatorStyle::NEVER));
        } else {
            ret += QStringLiteral("&amount=%1").arg(BitcoinUnits::format(BitcoinUnit::ORU, info.amount, false, BitcoinUnits::SeparatorStyle::NEVER));
        }
    }

    if (!info.label.isEmpty())
    {
        QString lbl(QUrl::toPercentEncoding(info.label));
        ret += QStringLiteral("&label=%1").arg(lbl);
    }

    if (!info.message.isEmpty())
    {
        QString msg(QUrl::toPercentEncoding(info.message));
        ret += QStringLiteral("&message=%1").arg(msg);
    }

    return ret;
}

QString FormatRecipientAssetDomainSummary(const QString& address)
{
    std::string dec_err;
    const CTxDestination dest = DecodeDestination(address.toStdString(), dec_err, nullptr);
    if (!IsValidDestination(dest)) {
        return QObject::tr("(invalid address)");
    }
    const CScript spk = GetScriptForDestination(dest);
    const AssetType at = AssetTypeFromScriptPubKey(spk);

    QString inferred;
    switch (at) {
    case ASSET_ORU: inferred = QObject::tr("ORU-domain"); break;
    case ASSET_RES: inferred = QObject::tr("RES"); break;
    default: inferred = QObject::tr("none / data"); break;
    }

    int witness_ver = -1;
    QString encoding = QObject::tr("Base58 (non-segwit)");
    int wv = 0;
    std::vector<unsigned char> wp;
    if (spk.IsWitnessProgram(wv, wp)) {
        witness_ver = wv;
        if (wv == 0) {
            encoding = QObject::tr("Bech32");
        } else if (wv == 1) {
            encoding = QObject::tr("Bech32m");
        } else {
            encoding = QObject::tr("Segwit (other)");
        }
    }

    const QString wv_text = (witness_ver >= 0) ? QString::number(witness_ver) : QObject::tr("n/a");
    return QObject::tr("Inferred type: %1; witness version: %2; encoding: %3")
        .arg(inferred, wv_text, encoding);
}

bool isDust(interfaces::Node& node, const QString& address, const CAmount& amount)
{
    CTxDestination dest = DecodeDestination(address.toStdString());
    CScript script = GetScriptForDestination(dest);
    CTxOut txOut(amount, script);
    return IsDust(txOut, node.getDustRelayFee());
}

QString HtmlEscape(const QString& str, bool fMultiLine)
{
    QString escaped = str.toHtmlEscaped();
    if(fMultiLine)
    {
        escaped = escaped.replace("\n", "<br>\n");
    }
    return escaped;
}

QString HtmlEscape(const std::string& str, bool fMultiLine)
{
    return HtmlEscape(QString::fromStdString(str), fMultiLine);
}

NativeReceiveAssetKind ClassifyNativeReceiveAddress(const QString& address)
{
    std::string err;
    const CTxDestination dest = DecodeDestination(address.toStdString(), err);
    if (!IsValidDestination(dest)) {
        return NativeReceiveAssetKind::Invalid;
    }
    if (std::holds_alternative<WitnessV0KeyHash>(dest) ||
        std::holds_alternative<WitnessV0ScriptHash>(dest)) {
        return NativeReceiveAssetKind::ORU;
    }
    if (std::holds_alternative<WitnessV1Res>(dest)) {
        return NativeReceiveAssetKind::RES;
    }
    return NativeReceiveAssetKind::Other;
}

void copyEntryData(const QAbstractItemView *view, int column, int role)
{
    if(!view || !view->selectionModel())
        return;
    QModelIndexList selection = view->selectionModel()->selectedRows(column);

    if(!selection.isEmpty())
    {
        // Copy first item
        setClipboard(selection.at(0).data(role).toString());
    }
}

QList<QModelIndex> getEntryData(const QAbstractItemView *view, int column)
{
    if(!view || !view->selectionModel())
        return QList<QModelIndex>();
    return view->selectionModel()->selectedRows(column);
}

bool hasEntryData(const QAbstractItemView *view, int column, int role)
{
    QModelIndexList selection = getEntryData(view, column);
    if (selection.isEmpty()) return false;
    return !selection.at(0).data(role).toString().isEmpty();
}

void LoadFont(const QString& file_name)
{
    const int id = QFontDatabase::addApplicationFont(file_name);
    if (id == -1) {
        qWarning() << "Failed to load application font" << file_name;
#ifdef WIN32
        MessageBoxW(nullptr,
                    L"Failed to load the embedded UI font. Continuing with system fonts.",
                    L"Rigor Wallet",
                    MB_OK | MB_ICONWARNING);
#endif
        return;
    }
}

namespace {
QString g_ui_font_family_css;

bool TryLoadApplicationFont(const QString& path)
{
    if (path.isEmpty() || !QFile::exists(path)) {
        return false;
    }
    return QFontDatabase::addApplicationFont(path) != -1;
}

void LoadKnownCjkFonts()
{
    // Prefer system CJK UI fonts so zh_CN (and other CJK) is not tofu boxes.
    // Also try WSL mount of Windows Fonts when running a Linux build under WSL.
    const QStringList candidates = {
#ifdef WIN32
        QStringLiteral("C:/Windows/Fonts/msyh.ttc"),
        QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"),
        QStringLiteral("C:/Windows/Fonts/msyhl.ttc"),
        QStringLiteral("C:/Windows/Fonts/simsun.ttc"),
        QStringLiteral("C:/Windows/Fonts/NotoSansSC-VF.ttf"),
        QStringLiteral("C:/Windows/Fonts/NotoSansSC-Regular.otf"),
        QStringLiteral("C:/Windows/Fonts/NotoSansCJKsc-Regular.otf"),
#else
        QStringLiteral("/mnt/c/Windows/Fonts/msyh.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/msyhbd.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/msyhl.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/simsun.ttc"),
        QStringLiteral("/mnt/c/Windows/Fonts/NotoSansSC-VF.ttf"),
        QStringLiteral("/mnt/c/Windows/Fonts/NotoSansSC-Regular.otf"),
        QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
        QStringLiteral("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"),
        QStringLiteral("/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf"),
        QStringLiteral("/usr/share/fonts/truetype/wqy/wqy-microhei.ttc"),
        QStringLiteral("/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"),
        QStringLiteral("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"),
#endif
    };
    for (const QString& path : candidates) {
        TryLoadApplicationFont(path);
    }
}

QStringList PreferAvailableFamilies(const QStringList& preferred)
{
    QStringList out;
    const QStringList available = QFontDatabase().families();
    for (const QString& family : preferred) {
        // Exact match or prefix (variable fonts / localized names).
        if (available.contains(family)) {
            out << family;
            continue;
        }
        for (const QString& avail : available) {
            if (avail.startsWith(family) || family.startsWith(avail)) {
                if (!out.contains(avail)) {
                    out << avail;
                }
                break;
            }
        }
    }
    return out;
}
} // namespace

void SetupUiFonts(const QString& lang_territory)
{
    LoadKnownCjkFonts();

    // Latin UI first for Latin glyphs; CJK fonts next so missing glyphs fall back.
    // When the UI language is Chinese, put a CJK UI face first to avoid tofu boxes
    // on platforms where default Latin fonts do not compose CJK fallbacks reliably.
    const QString lang = lang_territory.isEmpty() ? QLocale().name() : lang_territory;
    const bool prefer_cjk = lang.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive);

    const QStringList cjk_prefer = {
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei"),
        QStringLiteral("WenQuanYi Zen Hei"),
        QStringLiteral("Droid Sans Fallback"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Hiragino Sans GB"),
        QStringLiteral("SimHei"),
        QStringLiteral("SimSun"),
    };
    const QStringList latin_prefer = {
        QStringLiteral("Segoe UI"),
        QStringLiteral("Ubuntu"),
        QStringLiteral("Cantarell"),
        QStringLiteral("Noto Sans"),
        QStringLiteral("DejaVu Sans"),
        QStringLiteral("Sans Serif"),
    };

    QStringList families;
    const QStringList cjk_avail = PreferAvailableFamilies(cjk_prefer);
    const QStringList latin_avail = PreferAvailableFamilies(latin_prefer);
    if (prefer_cjk) {
        families << cjk_avail << latin_avail;
    } else {
        families << latin_avail << cjk_avail;
    }

    QFont app_font = QApplication::font();
    if (!families.contains(app_font.family())) {
        families << app_font.family();
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5, 13, 0))
    if (!families.isEmpty()) {
        app_font.setFamilies(families);
    }
#else
    if (!families.isEmpty()) {
        app_font.setFamily(families.first());
    }
#endif
    QApplication::setFont(app_font);

    // Stylesheets that hardcode Segoe UI / Roboto need an explicit CJK substitute.
    if (!cjk_avail.isEmpty()) {
        const QString cjk = cjk_avail.first();
        for (const QString& latin : {QStringLiteral("Segoe UI"), QStringLiteral("Roboto"),
                                    QStringLiteral("Inter"), QStringLiteral("Ubuntu"),
                                    QStringLiteral("Cantarell"), QStringLiteral("Sans Serif")}) {
            QFont::insertSubstitution(latin, cjk);
        }
    }

    QStringList css_quoted;
    for (const QString& family : families) {
        css_quoted << QStringLiteral("\"%1\"").arg(family);
    }
    css_quoted << QStringLiteral("sans-serif");
    g_ui_font_family_css = css_quoted.join(QStringLiteral(", "));
}

QString UiFontFamilyCss()
{
    if (g_ui_font_family_css.isEmpty()) {
        return QStringLiteral(
            "\"Segoe UI\", \"Microsoft YaHei UI\", \"Microsoft YaHei\", "
            "\"Noto Sans SC\", \"Noto Sans CJK SC\", \"WenQuanYi Micro Hei\", sans-serif");
    }
    return g_ui_font_family_css;
}

QString getDefaultDataDirectory()
{
    return PathToQString(GetDefaultDataDir());
}

QString ExtractFirstSuffixFromFilter(const QString& filter)
{
    QRegularExpression filter_re(QStringLiteral(".* \\(\\*\\.(.*)[ \\)]"), QRegularExpression::InvertedGreedinessOption);
    QString suffix;
    QRegularExpressionMatch m = filter_re.match(filter);
    if (m.hasMatch()) {
        suffix = m.captured(1);
    }
    return suffix;
}

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if(dir.isEmpty()) // Default to user documents location
    {
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result = QDir::toNativeSeparators(QFileDialog::getSaveFileName(parent, caption, myDir, filter, &selectedFilter));

    QString selectedSuffix = ExtractFirstSuffixFromFilter(selectedFilter);

    /* Add suffix if needed */
    QFileInfo info(result);
    if(!result.isEmpty())
    {
        if(info.suffix().isEmpty() && !selectedSuffix.isEmpty())
        {
            /* No suffix specified, add selected suffix */
            if(!result.endsWith("."))
                result.append(".");
            result.append(selectedSuffix);
        }
    }

    /* Return selected suffix if asked to */
    if(selectedSuffixOut)
    {
        *selectedSuffixOut = selectedSuffix;
    }
    return result;
}

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir,
    const QString &filter,
    QString *selectedSuffixOut)
{
    QString selectedFilter;
    QString myDir;
    if(dir.isEmpty()) // Default to user documents location
    {
        myDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    else
    {
        myDir = dir;
    }
    /* Directly convert path to native OS path separators */
    QString result = QDir::toNativeSeparators(QFileDialog::getOpenFileName(parent, caption, myDir, filter, &selectedFilter));

    if(selectedSuffixOut)
    {
        *selectedSuffixOut = ExtractFirstSuffixFromFilter(selectedFilter);
        ;
    }
    return result;
}

Qt::ConnectionType blockingGUIThreadConnection()
{
    if(QThread::currentThread() != qApp->thread())
    {
        return Qt::BlockingQueuedConnection;
    }
    else
    {
        return Qt::DirectConnection;
    }
}

bool checkPoint(const QPoint &p, const QWidget *w)
{
    QWidget *atW = QApplication::widgetAt(w->mapToGlobal(p));
    if (!atW) return false;
    return atW->window() == w;
}

bool isObscured(QWidget *w)
{
    return !(checkPoint(QPoint(0, 0), w)
        && checkPoint(QPoint(w->width() - 1, 0), w)
        && checkPoint(QPoint(0, w->height() - 1), w)
        && checkPoint(QPoint(w->width() - 1, w->height() - 1), w)
        && checkPoint(QPoint(w->width() / 2, w->height() / 2), w));
}

void bringToFront(QWidget* w)
{
#ifdef Q_OS_MACOS
    ForceActivation();
#endif

    if (w) {
        // activateWindow() (sometimes) helps with keyboard focus on Windows
        if (w->isMinimized()) {
            w->showNormal();
        } else {
            w->show();
        }
        w->activateWindow();
        w->raise();
    }
}

void handleCloseWindowShortcut(QWidget* w)
{
    QObject::connect(new QShortcut(QKeySequence(QObject::tr("Ctrl+W")), w), &QShortcut::activated, w, &QWidget::close);
}

void openDebugLogfile()
{
    fs::path pathDebug = gArgs.GetDataDirNet() / "debug.log";

    /* Open debug.log with the associated application */
    if (fs::exists(pathDebug))
        QDesktopServices::openUrl(QUrl::fromLocalFile(PathToQString(pathDebug)));
}

bool openBitcoinConf()
{
    fs::path pathConfig = gArgs.GetConfigFilePath();

    /* Create the file */
    std::ofstream configFile{pathConfig, std::ios_base::app};

    if (!configFile.good())
        return false;

    configFile.close();

    /* Open rigor.conf with the associated application */
    bool res = QDesktopServices::openUrl(QUrl::fromLocalFile(PathToQString(pathConfig)));
#ifdef Q_OS_MACOS
    // Workaround for macOS-specific behavior; see #15409.
    if (!res) {
        res = QProcess::startDetached("/usr/bin/open", QStringList{"-t", PathToQString(pathConfig)});
    }
#endif

    return res;
}

ToolTipToRichTextFilter::ToolTipToRichTextFilter(int _size_threshold, QObject *parent) :
    QObject(parent),
    size_threshold(_size_threshold)
{

}

bool ToolTipToRichTextFilter::eventFilter(QObject *obj, QEvent *evt)
{
    if(evt->type() == QEvent::ToolTipChange)
    {
        QWidget *widget = static_cast<QWidget*>(obj);
        QString tooltip = widget->toolTip();
        if(tooltip.size() > size_threshold && !tooltip.startsWith("<qt") && !Qt::mightBeRichText(tooltip))
        {
            // Envelop with <qt></qt> to make sure Qt detects this as rich text
            // Escape the current message as HTML and replace \n by <br>
            tooltip = "<qt>" + HtmlEscape(tooltip, true) + "</qt>";
            widget->setToolTip(tooltip);
            return true;
        }
    }
    return QObject::eventFilter(obj, evt);
}

LabelOutOfFocusEventFilter::LabelOutOfFocusEventFilter(QObject* parent)
    : QObject(parent)
{
}

bool LabelOutOfFocusEventFilter::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusOut) {
        auto focus_out = static_cast<QFocusEvent*>(event);
        if (focus_out->reason() != Qt::PopupFocusReason) {
            auto label = qobject_cast<QLabel*>(watched);
            if (label) {
                auto flags = label->textInteractionFlags();
                label->setTextInteractionFlags(Qt::NoTextInteraction);
                label->setTextInteractionFlags(flags);
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

#ifdef WIN32
fs::path static StartupShortcutPath()
{
    std::string chain = gArgs.GetChainName();
    if (chain == CBaseChainParams::MAIN)
        return GetSpecialFolderPath(CSIDL_STARTUP) / "Rigor.lnk";
    if (chain == CBaseChainParams::TESTNET) // Remove this special case when CBaseChainParams::TESTNET = "testnet4"
        return GetSpecialFolderPath(CSIDL_STARTUP) / "Rigor (testnet).lnk";
    return GetSpecialFolderPath(CSIDL_STARTUP) / fs::u8path(strprintf("Rigor (%s).lnk", chain));
}

bool GetStartOnSystemStartup()
{
    // check for Rigor*.lnk
    return fs::exists(StartupShortcutPath());
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    // If the shortcut exists already, remove it for updating
    fs::remove(StartupShortcutPath());

    if (fAutoStart)
    {
        CoInitialize(nullptr);

        // Get a pointer to the IShellLink interface.
        IShellLinkW* psl = nullptr;
        HRESULT hres = CoCreateInstance(CLSID_ShellLink, nullptr,
            CLSCTX_INPROC_SERVER, IID_IShellLinkW,
            reinterpret_cast<void**>(&psl));

        if (SUCCEEDED(hres))
        {
            // Get the current executable path
            WCHAR pszExePath[MAX_PATH];
            GetModuleFileNameW(nullptr, pszExePath, ARRAYSIZE(pszExePath));

            // Start client minimized
            QString strArgs = "-min";
            // Set -testnet /-regtest options
            strArgs += QString::fromStdString(strprintf(" -chain=%s", gArgs.GetChainName()));

            // Set the path to the shortcut target
            psl->SetPath(pszExePath);
            PathRemoveFileSpecW(pszExePath);
            psl->SetWorkingDirectory(pszExePath);
            psl->SetShowCmd(SW_SHOWMINNOACTIVE);
            psl->SetArguments(strArgs.toStdWString().c_str());

            // Query IShellLink for the IPersistFile interface for
            // saving the shortcut in persistent storage.
            IPersistFile* ppf = nullptr;
            hres = psl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&ppf));
            if (SUCCEEDED(hres))
            {
                // Save the link by calling IPersistFile::Save.
                hres = ppf->Save(StartupShortcutPath().wstring().c_str(), TRUE);
                ppf->Release();
                psl->Release();
                CoUninitialize();
                return true;
            }
            psl->Release();
        }
        CoUninitialize();
        return false;
    }
    return true;
}
#elif defined(Q_OS_LINUX)

// Follow the Desktop Application Autostart Spec:
// https://specifications.freedesktop.org/autostart-spec/autostart-spec-latest.html

fs::path static GetAutostartDir()
{
    char* pszConfigHome = getenv("XDG_CONFIG_HOME");
    if (pszConfigHome) return fs::path(pszConfigHome) / "autostart";
    char* pszHome = getenv("HOME");
    if (pszHome) return fs::path(pszHome) / ".config" / "autostart";
    return fs::path();
}

fs::path static GetAutostartFilePath()
{
    std::string chain = gArgs.GetChainName();
    if (chain == CBaseChainParams::MAIN)
        return GetAutostartDir() / "rigor.desktop";
    return GetAutostartDir() / fs::u8path(strprintf("rigor-%s.desktop", chain));
}

bool GetStartOnSystemStartup()
{
    std::ifstream optionFile{GetAutostartFilePath()};
    if (!optionFile.good())
        return false;
    // Scan through file for "Hidden=true":
    std::string line;
    while (!optionFile.eof())
    {
        getline(optionFile, line);
        if (line.find("Hidden") != std::string::npos &&
            line.find("true") != std::string::npos)
            return false;
    }
    optionFile.close();

    return true;
}

bool SetStartOnSystemStartup(bool fAutoStart)
{
    if (!fAutoStart)
        fs::remove(GetAutostartFilePath());
    else
    {
        char pszExePath[MAX_PATH+1];
        ssize_t r = readlink("/proc/self/exe", pszExePath, sizeof(pszExePath));
        if (r == -1 || r > MAX_PATH) {
            return false;
        }
        pszExePath[r] = '\0';

        fs::create_directories(GetAutostartDir());

        std::ofstream optionFile{GetAutostartFilePath(), std::ios_base::out | std::ios_base::trunc};
        if (!optionFile.good())
            return false;
        std::string chain = gArgs.GetChainName();
        // Write a rigor.desktop file to the autostart directory:
        optionFile << "[Desktop Entry]\n";
        optionFile << "Type=Application\n";
        if (chain == CBaseChainParams::MAIN)
            optionFile << "Name=Rigor\n";
        else
            optionFile << strprintf("Name=Rigor (%s)\n", chain);
        optionFile << "Exec=" << pszExePath << strprintf(" -min -chain=%s\n", chain);
        optionFile << "Terminal=false\n";
        optionFile << "Hidden=false\n";
        optionFile.close();
    }
    return true;
}

#else

bool GetStartOnSystemStartup() { return false; }
bool SetStartOnSystemStartup(bool fAutoStart) { return false; }

#endif

void setClipboard(const QString& str)
{
    QClipboard* clipboard = QApplication::clipboard();
    clipboard->setText(str, QClipboard::Clipboard);
    if (clipboard->supportsSelection()) {
        clipboard->setText(str, QClipboard::Selection);
    }
}

fs::path QStringToPath(const QString &path)
{
    return fs::u8path(path.toStdString());
}

QString PathToQString(const fs::path &path)
{
    return QString::fromStdString(path.u8string());
}

QString NetworkToQString(Network net)
{
    switch (net) {
    case NET_UNROUTABLE: return QObject::tr("Unroutable");
    //: Name of IPv4 network in peer info
    case NET_IPV4: return QObject::tr("IPv4", "network name");
    //: Name of IPv6 network in peer info
    case NET_IPV6: return QObject::tr("IPv6", "network name");
    //: Name of Tor network in peer info
    case NET_ONION: return QObject::tr("Onion", "network name");
    //: Name of I2P network in peer info
    case NET_I2P: return QObject::tr("I2P", "network name");
    //: Name of CJDNS network in peer info
    case NET_CJDNS: return QObject::tr("CJDNS", "network name");
    case NET_INTERNAL: return "Internal";  // should never actually happen
    case NET_MAX: assert(false);
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}

QString ConnectionTypeToQString(ConnectionType conn_type, bool prepend_direction)
{
    QString prefix;
    if (prepend_direction) {
        prefix = (conn_type == ConnectionType::INBOUND) ?
                     /*: An inbound connection from a peer. An inbound connection
                         is a connection initiated by a peer. */
                     QObject::tr("Inbound") :
                     /*: An outbound connection to a peer. An outbound connection
                         is a connection initiated by us. */
                     QObject::tr("Outbound") + " ";
    }
    switch (conn_type) {
    case ConnectionType::INBOUND: return prefix;
    //: Peer connection type that relays all network information.
    case ConnectionType::OUTBOUND_FULL_RELAY: return prefix + QObject::tr("Full Relay");
    /*: Peer connection type that relays network information about
        blocks and not transactions or addresses. */
    case ConnectionType::BLOCK_RELAY: return prefix + QObject::tr("Block Relay");
    //: Peer connection type established manually through one of several methods.
    case ConnectionType::MANUAL: return prefix + QObject::tr("Manual");
    //: Short-lived peer connection type that tests the aliveness of known addresses.
    case ConnectionType::FEELER: return prefix + QObject::tr("Feeler");
    //: Short-lived peer connection type that solicits known addresses from a peer.
    case ConnectionType::ADDR_FETCH: return prefix + QObject::tr("Address Fetch");
    } // no default case, so the compiler can warn about missing cases
    assert(false);
    return {}; // silence -Wreturn-type (assert may be compiled out)
}

QString formatDurationStr(std::chrono::seconds dur)
{
    using days = std::chrono::duration<int, std::ratio<86400>>; // can remove this line after C++20
    const auto d{std::chrono::duration_cast<days>(dur)};
    const auto h{std::chrono::duration_cast<std::chrono::hours>(dur - d)};
    const auto m{std::chrono::duration_cast<std::chrono::minutes>(dur - d - h)};
    const auto s{std::chrono::duration_cast<std::chrono::seconds>(dur - d - h - m)};
    QStringList str_list;
    if (auto d2{d.count()}) str_list.append(QObject::tr("%1 d").arg(d2));
    if (auto h2{h.count()}) str_list.append(QObject::tr("%1 h").arg(h2));
    if (auto m2{m.count()}) str_list.append(QObject::tr("%1 m").arg(m2));
    const auto s2{s.count()};
    if (s2 || str_list.empty()) str_list.append(QObject::tr("%1 s").arg(s2));
    return str_list.join(" ");
}

QString FormatPeerAge(std::chrono::seconds time_connected)
{
    const auto time_now{GetTime<std::chrono::seconds>()};
    const auto age{time_now - time_connected};
    if (age >= 24h) return QObject::tr("%1 d").arg(age / 24h);
    if (age >= 1h) return QObject::tr("%1 h").arg(age / 1h);
    if (age >= 1min) return QObject::tr("%1 m").arg(age / 1min);
    return QObject::tr("%1 s").arg(age / 1s);
}

QString formatServicesStr(quint64 mask)
{
    QStringList strList;

    for (const auto& flag : serviceFlagsToStr(mask)) {
        strList.append(QString::fromStdString(flag));
    }

    if (strList.size())
        return strList.join(", ");
    else
        return QObject::tr("None");
}

QString formatPingTime(std::chrono::microseconds ping_time)
{
    return (ping_time == std::chrono::microseconds::max() || ping_time == 0us) ?
        QObject::tr("N/A") :
        QObject::tr("%1 ms").arg(QString::number((int)(count_microseconds(ping_time) / 1000), 10));
}

QString formatTimeOffset(int64_t nTimeOffset)
{
  return QObject::tr("%1 s").arg(QString::number((int)nTimeOffset, 10));
}

QString formatNiceTimeOffset(qint64 secs)
{
    // Represent time from last generated block in human readable text
    QString timeBehindText;
    const int HOUR_IN_SECONDS = 60*60;
    const int DAY_IN_SECONDS = 24*60*60;
    const int WEEK_IN_SECONDS = 7*24*60*60;
    const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
    if(secs < 60)
    {
        timeBehindText = QObject::tr("%n second(s)","",secs);
    }
    else if(secs < 2*HOUR_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n minute(s)","",secs/60);
    }
    else if(secs < 2*DAY_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n hour(s)","",secs/HOUR_IN_SECONDS);
    }
    else if(secs < 2*WEEK_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n day(s)","",secs/DAY_IN_SECONDS);
    }
    else if(secs < YEAR_IN_SECONDS)
    {
        timeBehindText = QObject::tr("%n week(s)","",secs/WEEK_IN_SECONDS);
    }
    else
    {
        qint64 years = secs / YEAR_IN_SECONDS;
        qint64 remainder = secs % YEAR_IN_SECONDS;
        timeBehindText = QObject::tr("%1 and %2").arg(QObject::tr("%n year(s)", "", years)).arg(QObject::tr("%n week(s)","", remainder/WEEK_IN_SECONDS));
    }
    return timeBehindText;
}

QString formatBytes(uint64_t bytes)
{
    if (bytes < 1'000)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1'000'000)
        return QObject::tr("%1 kB").arg(bytes / 1'000);
    if (bytes < 1'000'000'000)
        return QObject::tr("%1 MB").arg(bytes / 1'000'000);

    return QObject::tr("%1 GB").arg(bytes / 1'000'000'000);
}

qreal calculateIdealFontSize(int width, const QString& text, QFont font, qreal minPointSize, qreal font_size) {
    while(font_size >= minPointSize) {
        font.setPointSizeF(font_size);
        QFontMetrics fm(font);
        if (TextWidth(fm, text) < width) {
            break;
        }
        font_size -= 0.5;
    }
    return font_size;
}

ThemedLabel::ThemedLabel(const PlatformStyle* platform_style, QWidget* parent)
    : QLabel{parent}, m_platform_style{platform_style}
{
    assert(m_platform_style);
}

void ThemedLabel::setThemedPixmap(const QString& image_filename, int width, int height)
{
    m_image_filename = image_filename;
    m_pixmap_width = width;
    m_pixmap_height = height;
    updateThemedPixmap();
}

void ThemedLabel::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        updateThemedPixmap();
    }

    QLabel::changeEvent(e);
}

void ThemedLabel::updateThemedPixmap()
{
    setPixmap(m_platform_style->SingleColorIcon(m_image_filename).pixmap(m_pixmap_width, m_pixmap_height));
}

ClickableLabel::ClickableLabel(const PlatformStyle* platform_style, QWidget* parent)
    : ThemedLabel{platform_style, parent}
{
}

void ClickableLabel::mouseReleaseEvent(QMouseEvent *event)
{
    Q_EMIT clicked(event->pos());
}

void ClickableProgressBar::mouseReleaseEvent(QMouseEvent *event)
{
    Q_EMIT clicked(event->pos());
}

bool ItemDelegate::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        if (static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            Q_EMIT keyEscapePressed();
        }
    }
    return QItemDelegate::eventFilter(object, event);
}

void PolishProgressDialog(QProgressDialog* dialog)
{
#ifdef Q_OS_MACOS
    // Workaround for macOS-only Qt bug; see: QTBUG-65750, QTBUG-70357.
    const int margin = TextWidth(dialog->fontMetrics(), ("X"));
    dialog->resize(dialog->width() + 2 * margin, dialog->height());
#endif
    // QProgressDialog estimates the time the operation will take (based on time
    // for steps), and only shows itself if that estimate is beyond minimumDuration.
    // The default minimumDuration value is 4 seconds, and it could make users
    // think that the GUI is frozen.
    dialog->setMinimumDuration(0);
}

int TextWidth(const QFontMetrics& fm, const QString& text)
{
    return fm.horizontalAdvance(text);
}

void LogQtInfo()
{
#ifdef QT_STATIC
    const std::string qt_link{"static"};
#else
    const std::string qt_link{"dynamic"};
#endif
#ifdef QT_STATICPLUGIN
    const std::string plugin_link{"static"};
#else
    const std::string plugin_link{"dynamic"};
#endif
    LogPrintf("Qt %s (%s), plugin=%s (%s)\n", qVersion(), qt_link, QGuiApplication::platformName().toStdString(), plugin_link);
    const auto static_plugins = QPluginLoader::staticPlugins();
    if (static_plugins.empty()) {
        LogPrintf("No static plugins.\n");
    } else {
        LogPrintf("Static plugins:\n");
        for (const QStaticPlugin& p : static_plugins) {
            QJsonObject meta_data = p.metaData();
            const std::string plugin_class = meta_data.take(QString("className")).toString().toStdString();
            const int plugin_version = meta_data.take(QString("version")).toInt();
            LogPrintf(" %s, version %d\n", plugin_class, plugin_version);
        }
    }

    LogPrintf("Style: %s / %s\n", QApplication::style()->objectName().toStdString(), QApplication::style()->metaObject()->className());
    LogPrintf("System: %s, %s\n", QSysInfo::prettyProductName().toStdString(), QSysInfo::buildAbi().toStdString());
    for (const QScreen* s : QGuiApplication::screens()) {
        LogPrintf("Screen: %s %dx%d, pixel ratio=%.1f\n", s->name().toStdString(), s->size().width(), s->size().height(), s->devicePixelRatio());
    }
}

void PopupMenu(QMenu* menu, const QPoint& point, QAction* at_action)
{
    // The qminimal plugin does not provide window system integration.
    if (QApplication::platformName() == "minimal") return;
    menu->popup(point, at_action);
}

QDateTime StartOfDay(const QDate& date)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    return date.startOfDay();
#else
    return QDateTime(date);
#endif
}

bool HasPixmap(const QLabel* label)
{
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    return !label->pixmap(Qt::ReturnByValue).isNull();
#else
    return label->pixmap() != nullptr;
#endif
}

QImage GetImage(const QLabel* label)
{
    if (!HasPixmap(label)) {
        return QImage();
    }

#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
    return label->pixmap(Qt::ReturnByValue).toImage();
#else
    return label->pixmap()->toImage();
#endif
}

QString MakeHtmlLink(const QString& source, const QString& link)
{
    return QString(source).replace(
        link,
        QLatin1String("<a href=\"") + link + QLatin1String("\">") + link + QLatin1String("</a>"));
}

void PrintSlotException(
    const std::exception* exception,
    const QObject* sender,
    const QObject* receiver)
{
    std::string description = sender->metaObject()->className();
    description += "->";
    description += receiver->metaObject()->className();
    PrintExceptionContinue(exception, description);
}

void ShowModalDialogAsynchronously(QDialog* dialog)
{
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->show();
}

/** Checkbox/radio: paper idle, ink fill when checked. Do not rely on SVG images
 *  (Windows native style / static Qt often drops QSS `image:` and only draws a ring). */
static QString RigorCheckIndicatorStylesheet(const QString& selector_prefix)
{
    return QStringLiteral(
        "%1QCheckBox, %1QRadioButton {"
        "  color: #26394B; spacing: 6px; background: transparent;"
        "}"
        "%1QCheckBox:disabled, %1QRadioButton:disabled { color: #7A8794; }"
        "%1QCheckBox::indicator, %1QRadioButton::indicator {"
        "  width: 18px; height: 18px;"
        "  border: 2px solid #8A7E6C;"
        "  background-color: #F3E6D0;"
        "}"
        "%1QCheckBox::indicator { border-radius: 3px; }"
        "%1QRadioButton::indicator { border-radius: 9px; }"
        "%1QCheckBox::indicator:checked {"
        "  background-color: #26394B;"
        "  border: 2px solid #26394B;"
        "  image: none;"
        "}"
        "%1QRadioButton::indicator:checked {"
        "  background-color: #26394B;"
        "  border: 2px solid #26394B;"
        "  image: none;"
        "}"
        "%1QCheckBox::indicator:unchecked, %1QRadioButton::indicator:unchecked {"
        "  background-color: #F3E6D0;"
        "  border: 2px solid #8A7E6C;"
        "  image: none;"
        "}"
        "%1QCheckBox::indicator:disabled, %1QRadioButton::indicator:disabled {"
        "  background-color: #E4D5BE; border: 2px solid #C4B49C; image: none;"
        "}"
    ).arg(selector_prefix);
}

/** Warm field surface and blue-black ink for every editable field. */
static QString RigorEditableFieldStylesheet(const QString& selector_prefix, const QString& ui_font)
{
    return QStringLiteral(
        "%1QLineEdit, %1QPlainTextEdit, %1QTextEdit, %1QSpinBox, %1QDoubleSpinBox,"
        "%1QAbstractSpinBox, %1QAbstractSpinBox QLineEdit, %1QComboBox, %1QDateTimeEdit {"
        "  background-color: #F3E6D0;"
        "  color: #26394B;"
        "  border: 1px solid #8A7E6C;"
        "  border-radius: 4px;"
        "  padding: 4px 6px;"
        "  selection-background-color: #C4B398;"
        "  selection-color: #26394B;"
        "  font-family: %2;"
        "}"
        "%1QLineEdit:focus, %1QPlainTextEdit:focus, %1QTextEdit:focus, %1QSpinBox:focus,"
        "%1QComboBox:focus, %1QAbstractSpinBox:focus {"
        "  border: 1px solid #26394B;"
        "}"
        "%1QLineEdit:disabled, %1QComboBox:disabled, %1QSpinBox:disabled, %1QAbstractSpinBox:disabled {"
        "  background-color: #E4D5BE;"
        "  color: #7A8794;"
        "  border: 1px solid #C4B49C;"
        "}"
    ).arg(selector_prefix, ui_font);
}

QString RigorDialogControlStylesheet()
{
    const QString ui_font = UiFontFamilyCss();
    return QStringLiteral(
        "QDialog, QMessageBox {"
        "  background-color: #E3D1B8; color: #26394B; font-family: %1;"
        "}"
        "QDialog QWidget { background-color: #E3D1B8; color: #26394B; }"
        "QDialog QLabel, QMessageBox QLabel { color: #26394B; background: transparent; font-family: %1; }"
        "QDialog QGroupBox {"
        "  color: #26394B; background-color: #EADCC6; border: 1px solid #8A7E6C;"
        "  margin-top: 8px; padding-top: 8px; font-family: %1;"
        "}"
        "QDialog QGroupBox::title { color: #26394B; subcontrol-origin: margin; left: 8px; }"
        "QDialog QTabWidget::pane {"
        "  background-color: #E3D1B8; border: 1px solid #8A7E6C; top: -1px;"
        "}"
        "QDialog QTabBar::tab {"
        "  background: #EADCC6; color: #526477; border: 1px solid #8A7E6C;"
        "  padding: 6px 12px; margin-right: 2px; font-family: %1;"
        "}"
        "QDialog QTabBar::tab:selected { background: #C4B398; color: #26394B; }"
        "QDialog QTabBar::tab:hover:!selected { background: #DDD0B8; color: #26394B; }"
        "QDialog QScrollArea, QDialog QAbstractScrollArea {"
        "  background-color: #E3D1B8; color: #26394B; border: none;"
        "}"
    ).arg(ui_font)
    + RigorCheckIndicatorStylesheet(QStringLiteral("QDialog "))
    + RigorEditableFieldStylesheet(QStringLiteral("QDialog "), ui_font)
    + QStringLiteral(
        "QDialog QComboBox::drop-down { border: none; width: 20px; background: transparent; }"
        "QDialog QComboBox QAbstractItemView {"
        "  background-color: #F3E6D0; color: #26394B;"
        "  selection-background-color: #C4B398; selection-color: #26394B;"
        "  font-family: %1;"
        "}"
        "QDialog QListView, QDialog QTreeView, QDialog QTableView {"
        "  background-color: #F3E6D0; alternate-background-color: #EAD9C1; color: #26394B;"
        "  border: 1px solid #8A7E6C; gridline-color: #B6A58C;"
        "  selection-background-color: #C4B398; selection-color: #26394B;"
        "}"
        "QDialog QHeaderView::section {"
        "  background: #EADCC6; color: #26394B; border: 1px solid #8A7E6C; padding: 4px;"
        "}"
        "QDialog QPushButton, QMessageBox QPushButton {"
        "  background-color: #EADCC6;"
        "  color: #26394B;"
        "  border: 1px solid #8A7E6C;"
        "  border-radius: 4px;"
        "  padding: 6px 16px;"
        "  min-height: 26px;"
        "  min-width: 80px;"
        "  font-family: %1;"
        "}"
        "QDialog QPushButton:hover:!disabled, QMessageBox QPushButton:hover:!disabled {"
        "  background-color: #DDD0B8; color: #26394B;"
        "}"
        "QDialog QPushButton:pressed { background-color: #C4B398; }"
        "QDialog QPushButton:disabled, QMessageBox QPushButton:disabled {"
        "  background-color: #E4D5BE;"
        "  color: #7A8794;"
        "  border: 1px solid #C4B49C;"
        "}"
        "QDialog QPushButton:default { border: 2px solid #26394B; font-weight: 600; }"
        "QDialog QDialogButtonBox QPushButton { min-width: 88px; }"
        "QDialog QScrollBar:vertical { background: #E3D1B8; width: 12px; }"
        "QDialog QScrollBar::handle:vertical { background: #8A7E6C; min-height: 20px; border-radius: 4px; }"
        "QDialog QScrollBar:horizontal { background: #E3D1B8; height: 12px; }"
        "QDialog QScrollBar::handle:horizontal { background: #8A7E6C; min-width: 20px; border-radius: 4px; }"
    ).arg(ui_font);
}

void ApplyRigorNativeCaption(QWidget* widget)
{
    if (!widget) return;
#ifdef WIN32
    widget->winId();
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd) return;
    HMODULE lib = GetModuleHandleW(L"dwmapi.dll");
    if (!lib) lib = LoadLibraryW(L"dwmapi.dll");
    if (!lib) return;
    using SetAttrFn = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    auto set_attr = reinterpret_cast<SetAttrFn>(GetProcAddress(lib, "DwmSetWindowAttribute"));
    if (!set_attr) return;
    // Windows 11 22000+. Windows 10 returns an error and keeps the system caption.
    const COLORREF caption = RGB(0xD8, 0xC5, 0xA8);
    const COLORREF text = RGB(0x26, 0x39, 0x4B);
    constexpr DWORD DWMWA_CAPTION_COLOR = 35;
    constexpr DWORD DWMWA_TEXT_COLOR = 36;
    set_attr(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    set_attr(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
#else
    Q_UNUSED(widget);
#endif
}

namespace {

class RigorEdgeHandle : public QWidget
{
public:
    RigorEdgeHandle(QWidget* window, Qt::Edges edges, QWidget* parent)
        : QWidget(parent), m_window(window), m_edges(edges)
    {
        setStyleSheet(QStringLiteral("background: transparent;"));
        if (edges == Qt::LeftEdge || edges == Qt::RightEdge) setCursor(Qt::SizeHorCursor);
        else if (edges == Qt::TopEdge || edges == Qt::BottomEdge) setCursor(Qt::SizeVerCursor);
        else if (edges == (Qt::TopEdge | Qt::LeftEdge) || edges == (Qt::BottomEdge | Qt::RightEdge)) setCursor(Qt::SizeFDiagCursor);
        else setCursor(Qt::SizeBDiagCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_window->windowHandle() && !m_window->isMaximized()) {
            m_window->windowHandle()->startSystemResize(m_edges);
        } else {
            QWidget::mousePressEvent(event);
        }
    }

private:
    QWidget* m_window;
    Qt::Edges m_edges;
};

class RigorCaptionBar : public QWidget
{
public:
    RigorCaptionBar(QWidget* window, QWidget* parent)
        : QWidget(parent), m_window(window)
    {
        setObjectName(QStringLiteral("RigorCaptionBar"));
        setFixedHeight(32);
        setStyleSheet(QStringLiteral(
            "QWidget#RigorCaptionBar { background: #E3D1B8; border-bottom: 1px solid #8A7E6C; }"
            "QLabel { background: transparent; color: #26394B; }"));
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(10, 0, 0, 0);
        lay->setSpacing(0);
        m_icon = new QLabel(this);
        m_icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_title = new QLabel(window->windowTitle(), this);
        m_title->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(m_icon);
        lay->addSpacing(8);
        lay->addWidget(m_title, 1);
        m_min = makeButton(QStringLiteral("–"));
        m_max = makeButton(QStringLiteral("□"));
        m_close = makeButton(QStringLiteral("×"));
        m_close->setStyleSheet(QStringLiteral(
            "QToolButton { background: #E3D1B8; color: #26394B; border: none; padding: 0 14px; }"
            "QToolButton:hover { background: #6A3018; color: #F4E8D6; }"));
        lay->addWidget(m_min);
        lay->addWidget(m_max);
        lay->addWidget(m_close);
        connect(m_min, &QToolButton::clicked, window, &QWidget::showMinimized);
        connect(m_max, &QToolButton::clicked, window, [this] { toggleMaximized(); });
        connect(m_close, &QToolButton::clicked, window, &QWidget::close);
        connect(window, &QWidget::windowTitleChanged, m_title, &QLabel::setText);
        connect(window, &QWidget::windowIconChanged, this, [this](const QIcon&) { refreshIcon(); });
        refreshIcon();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !m_window->windowHandle()) {
            QWidget::mousePressEvent(event);
            return;
        }
        const int edge = 6;
        Qt::Edges edges;
        if (event->pos().y() <= edge) edges |= Qt::TopEdge;
        if (event->pos().x() <= edge) edges |= Qt::LeftEdge;
        if (event->pos().x() >= width() - edge) edges |= Qt::RightEdge;
        if (edges && !m_window->isMaximized()) m_window->windowHandle()->startSystemResize(edges);
        else m_window->windowHandle()->startSystemMove();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) toggleMaximized();
        else QWidget::mouseDoubleClickEvent(event);
    }

private:
    QToolButton* makeButton(const QString& text)
    {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::ArrowCursor);
        button->setStyleSheet(QStringLiteral(
            "QToolButton { background: #E3D1B8; color: #26394B; border: none; padding: 0 12px; }"
            "QToolButton:hover { background: #C4B398; }"));
        return button;
    }

    void toggleMaximized()
    {
        if (m_window->isMaximized()) m_window->showNormal();
        else m_window->showMaximized();
    }

    void refreshIcon()
    {
        const QIcon icon = m_window->windowIcon();
        if (icon.isNull()) {
            m_icon->clear();
            return;
        }
        m_icon->setPixmap(icon.pixmap(16, 16));
    }

    QWidget* m_window;
    QLabel* m_icon;
    QLabel* m_title;
    QToolButton* m_min;
    QToolButton* m_max;
    QToolButton* m_close;
};

class RigorCaptionFilter : public QObject
{
public:
    RigorCaptionFilter(QWidget* window, std::function<void()> place)
        : QObject(window), m_window(window), m_place(std::move(place)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_window && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            m_place();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QWidget* m_window;
    std::function<void()> m_place;
};

} // namespace

void InstallRigorParchmentCaption(QWidget* window)
{
    if (!window || !window->isWindow() || window->property("rigorCaption").toBool()) return;
    window->setProperty("rigorCaption", true);
    window->setWindowFlag(Qt::FramelessWindowHint, true);

    auto* bar = new RigorCaptionBar(window, window);
    if (auto* main_window = qobject_cast<QMainWindow*>(window)) {
        // Keep the existing menu bar object. setMenuWidget() would delete it and
        // leave the window-menu actions dangling.
        QMenuBar* menu = qobject_cast<QMenuBar*>(main_window->menuWidget());
        if (QLayout* layout = main_window->layout()) layout->setMenuBar(nullptr);
        auto* wrap = new QWidget(main_window);
        wrap->setObjectName(QStringLiteral("RigorMenuWrap"));
        wrap->setStyleSheet(QStringLiteral("QWidget#RigorMenuWrap { background: #E3D1B8; }"));
        auto* column = new QVBoxLayout(wrap);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        bar->setParent(wrap);
        column->addWidget(bar);
        if (menu) {
            menu->setParent(wrap);
            menu->show();
            column->addWidget(menu);
        }
        main_window->setMenuWidget(wrap);
    } else if (QLayout* layout = window->layout()) {
        const QMargins layout_margins = layout->contentsMargins();
        layout->setContentsMargins(layout_margins.left(), layout_margins.top() + bar->height(), layout_margins.right(), layout_margins.bottom());
    } else {
        const QMargins margins = window->contentsMargins();
        window->setContentsMargins(margins.left(), margins.top() + bar->height(), margins.right(), margins.bottom());
    }

    const int thickness = 5;
    auto* left = new RigorEdgeHandle(window, Qt::LeftEdge, window);
    auto* right = new RigorEdgeHandle(window, Qt::RightEdge, window);
    auto* bottom = new RigorEdgeHandle(window, Qt::BottomEdge, window);
    auto place = [window, bar, left, right, bottom, thickness] {
        if (bar->parentWidget() == window) {
            bar->setGeometry(0, 0, window->width(), bar->height());
        }
        const int w = window->width();
        const int h = window->height();
        const int top = bar->height();
        left->setGeometry(0, top, thickness, qMax(0, h - top - thickness));
        right->setGeometry(qMax(0, w - thickness), top, thickness, qMax(0, h - top - thickness));
        bottom->setGeometry(0, qMax(0, h - thickness), w, thickness);
        left->raise();
        right->raise();
        bottom->raise();
        bar->raise();
    };
    place();
    window->installEventFilter(new RigorCaptionFilter(window, std::function<void()>(place)));
}

void ApplyRigorDialogChrome(QWidget* dialog)
{
    if (!dialog) return;
    dialog->setAutoFillBackground(true);
    QPalette pal = dialog->palette();
    pal.setColor(QPalette::Window, QColor(QStringLiteral("#E3D1B8")));
    pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Button, QColor(QStringLiteral("#EADCC6")));
    pal.setColor(QPalette::ButtonText, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Base, QColor(QStringLiteral("#F3E6D0")));
    pal.setColor(QPalette::Text, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Highlight, QColor(QStringLiteral("#C4B398")));
    pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#7A8794")));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#7A8794")));
    dialog->setPalette(pal);
    dialog->setStyleSheet(RigorDialogControlStylesheet());
    ApplyRigorNativeCaption(dialog);
    InstallRigorParchmentCaption(dialog);
}

void ApplyRigorWalletChrome(QMainWindow* mainWindow, QWidget* walletFrame, QWidget* rpcConsole)
{
    if (!mainWindow) return;

    QPalette pal = QGuiApplication::palette();
    pal.setColor(QPalette::Window, QColor(QStringLiteral("#E3D1B8")));
    pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Base, QColor(QStringLiteral("#F3E6D0")));
    pal.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#EAD9C1")));
    pal.setColor(QPalette::Text, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Button, QColor(QStringLiteral("#EADCC6")));
    pal.setColor(QPalette::ButtonText, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Highlight, QColor(QStringLiteral("#C4B398")));
    pal.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::Link, QColor(QStringLiteral("#3E5568")));
    pal.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#F3E6D0")));
    pal.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#26394B")));
    pal.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#7A8794")));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#7A8794")));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#7A8794")));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#7A8794")));
    QApplication::setPalette(pal);

    qApp->setStyleSheet(RigorDialogControlStylesheet());
    ApplyRigorNativeCaption(mainWindow);
    ApplyRigorNativeCaption(rpcConsole);
    InstallRigorParchmentCaption(mainWindow);
    if (rpcConsole && rpcConsole->isWindow()) InstallRigorParchmentCaption(rpcConsole);

    mainWindow->setObjectName(QStringLiteral("RigorMainWindow"));
    const QString ui_font = UiFontFamilyCss();
    mainWindow->setStyleSheet(QStringLiteral(
        "QMainWindow#RigorMainWindow {"
        "  background-color: #E3D1B8;"
        "  background-image: url(:/images/paper_grain);"
        "  background-repeat: repeat;"
        "  color: #26394B;"
        "  font-family: %1;"
        "}"
        "QMenuBar { background-color: #D8C5A8; color: #26394B; border-bottom: 1px solid #8A7E6C; font-family: %1; }"
        "QMenuBar::item:selected { background: #C4B398; }"
        "QMenuBar::item:pressed { background: #C4B398; }"
        "QMenu { background-color: #D8C5A8; color: #26394B; border: 1px solid #8A7E6C; font-family: %1; }"
        "QMenu::item:selected { background: #C4B398; color: #26394B; }"
        "QMenu::item:disabled { color: #7A8794; }"
        "QMenu::separator { height: 1px; background: #8A7E6C; margin: 4px 8px; }"
        "QToolBar#RigorNavSidebar {"
        "  background-color: #D6C3A5;"
        "  border: none;"
        "  border-right: 1px solid #8A7E6C;"
        "  spacing: 2px;"
        "  padding: 8px 6px;"
        "}"
        "QToolBar#RigorNavSidebar::separator { background: #8A7E6C; height: 1px; width: 160px; margin: 8px 10px; }"
        "QToolBar#RigorNavSidebar QToolButton {"
        "  color: #26394B;"
        "  background: transparent;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 7px 8px;"
        "  font-family: %1;"
        "}"
        "QToolBar#RigorNavSidebar QToolButton:hover { background: #E4D3B8; }"
        "QToolBar#RigorNavSidebar QToolButton:checked { background: #C4B398; color: #26394B; font-weight: 600; border-left: 3px solid #79664D; }"
        "QToolBar#RigorNavSidebar QToolButton:disabled { color: #7A8794; }"
        "QToolBar#RigorNavSidebar QLabel { color: #526477; background: transparent; padding: 2px 4px 0 4px; font-family: %1; }"
        "QStatusBar { background-color: #D8C5A8; color: #26394B; border-top: 1px solid #8A7E6C; font-family: %1; }"
        "QStatusBar QLabel { color: #26394B; background: transparent; }"
        "QProgressBar { border: 1px solid #8A7E6C; border-radius: 4px; text-align: center; background: #F3E6D0; color: #26394B; }"
        "QProgressBar::chunk { background-color: #6A3018; border-radius: 3px; }"
        "QPushButton {"
        "  background-color: #D8C5A8; color: #26394B; border: 1px solid #8A7E6C;"
        "  border-radius: 4px; padding: 6px 14px; min-height: 24px; font-family: %1;"
        "}"
        "QPushButton:hover:!disabled { background-color: #C4B398; color: #26394B; }"
        "QPushButton:pressed { background-color: #B8A486; }"
        "QPushButton:disabled { background-color: #E4D5BE; color: #7A8794; border: 1px solid #C4B49C; }"
        "QPushButton:default { border: 2px solid #26394B; }"
        "QToolTip { background-color: #F3E6D0; color: #26394B; border: 1px solid #8A7E6C; }"
        "QDialog, QDialog QLabel, QDialog QTabBar::tab, QDialog QCheckBox, QDialog QGroupBox,"
        "QDialog QComboBox, QDialog QLineEdit { font-family: %1; }"
    ).arg(ui_font));

    const QString wallet_ss = QStringLiteral(
        "QWidget#RigorWalletFrame {"
        "  background-color: #E3D1B8;"
        "  background-image: url(:/images/paper_grain);"
        "  background-repeat: repeat;"
        "  color: #26394B;"
        "  font-family: %1;"
        "}"
        "QStackedWidget { background: transparent; }"
        "QLabel { color: #26394B; font-family: %1; }"
        "QCheckBox { color: #26394B; font-family: %1; }"
        "QRadioButton { color: #26394B; font-family: %1; }"
        "QGroupBox { color: #26394B; background: #EADCC6; border: 1px solid #8A7E6C; margin-top: 8px; font-family: %1; }"
        "QGroupBox::title { color: #26394B; }"
        "QAbstractItemView { background: #F3E6D0; alternate-background-color: #EAD9C1; color: #26394B; "
        "  gridline-color: #B6A58C; selection-background-color: #C4B398; selection-color: #26394B; font-family: %1; }"
        "QHeaderView::section { background: #EADCC6; color: #26394B; border: 1px solid #8A7E6C; padding: 4px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #F3E6D0; color: #26394B; "
        "  selection-background-color: #C4B398; selection-color: #26394B; font-family: %1; }"
        "QTabWidget::pane { border: 1px solid #8A7E6C; background: #E3D1B8; }"
        "QTabBar::tab { background: #EADCC6; color: #526477; border: 1px solid #8A7E6C; padding: 6px 12px; font-family: %1; }"
        "QTabBar::tab:selected { background: #C4B398; color: #26394B; }"
        "QTabBar::tab:hover:!selected { background: #DDD0B8; color: #26394B; }"
        "QPushButton {"
        "  background-color: #D8C5A8; color: #26394B; border: 1px solid #8A7E6C;"
        "  border-radius: 4px; padding: 6px 14px; min-height: 24px; font-family: %1;"
        "}"
        "QPushButton:hover:!disabled { background-color: #C4B398; color: #26394B; }"
        "QPushButton:pressed { background-color: #B8A486; }"
        "QPushButton:disabled { background-color: #E4D5BE; color: #7A8794; border: 1px solid #C4B49C; }"
        "QScrollBar:vertical { background: #E3D1B8; width: 12px; }"
        "QScrollBar::handle:vertical { background: #8A7E6C; min-height: 20px; border-radius: 4px; }"
        "QScrollBar:horizontal { background: #E3D1B8; height: 12px; }"
        "QScrollBar::handle:horizontal { background: #8A7E6C; min-width: 20px; border-radius: 4px; }"
    ).arg(ui_font)
    + RigorCheckIndicatorStylesheet(QString())
    + RigorEditableFieldStylesheet(QString(), ui_font);
    if (walletFrame) {
        walletFrame->setStyleSheet(wallet_ss);
    }

    const QString rpc_ss = QStringLiteral(
        "QWidget#RigorRpcConsole {"
        "  background-color: #E3D1B8;"
        "  color: #26394B;"
        "}"
        "QWidget#RigorRpcConsole QWidget { background: transparent; color: #26394B; }"
        "QWidget#RigorRpcConsole QLabel { color: #26394B; background: transparent; }"
        "QWidget#RigorRpcConsole QGroupBox { color: #26394B; background: #EADCC6; border: 1px solid #8A7E6C; margin-top: 8px; }"
        "QWidget#RigorRpcConsole QTableView, QWidget#RigorRpcConsole QTreeWidget, QWidget#RigorRpcConsole QListWidget {"
        "  background: #F3E6D0; alternate-background-color: #EAD9C1; color: #26394B; "
        "  gridline-color: #B6A58C; selection-background-color: #C4B398; selection-color: #26394B;"
        "  border: 1px solid #8A7E6C;"
        "}"
        "QWidget#RigorRpcConsole QHeaderView::section { background: #EADCC6; color: #26394B; border: 1px solid #8A7E6C; }"
        "QWidget#RigorRpcConsole QTextEdit, QWidget#RigorRpcConsole QPlainTextEdit {"
        "  background-color: #E3D1B8; color: #26394B; border: 1px solid #8A7E6C; border-radius: 4px;"
        "  selection-background-color: #C4B398; selection-color: #26394B;"
        "  font-family: \"Roboto Mono\", Consolas, monospace;"
        "}"
        "QWidget#RigorRpcConsole QLineEdit, QWidget#RigorRpcConsole QSpinBox,"
        "QWidget#RigorRpcConsole QAbstractSpinBox, QWidget#RigorRpcConsole QComboBox {"
        "  background-color: #F3E6D0; color: #26394B; border: 1px solid #8A7E6C; border-radius: 4px; padding: 4px;"
        "  selection-background-color: #C4B398; selection-color: #26394B;"
        "}"
        "QWidget#RigorRpcConsole QLineEdit:focus { border: 1px solid #26394B; }"
        "QWidget#RigorRpcConsole QTabWidget::pane { border: 1px solid #8A7E6C; background: #E3D1B8; }"
        "QWidget#RigorRpcConsole QTabBar::tab { background: #EADCC6; color: #526477; border: 1px solid #8A7E6C; padding: 6px 10px; }"
        "QWidget#RigorRpcConsole QTabBar::tab:selected { background: #C4B398; color: #26394B; }"
        "QWidget#RigorRpcConsole QTabBar::tab:hover:!selected { background: #DDD0B8; color: #26394B; }"
        "QWidget#RigorRpcConsole QPushButton, QWidget#RigorRpcConsole QToolButton {"
        "  background-color: #D8C5A8; color: #26394B; border: 1px solid #8A7E6C; border-radius: 4px;"
        "  padding: 2px; min-width: 32px; min-height: 32px; font-weight: 600;"
        "}"
        "QWidget#RigorRpcConsole QToolButton#clearButton { border-radius: 16px; }"
        "QWidget#RigorRpcConsole QPushButton:hover:!disabled, QWidget#RigorRpcConsole QToolButton:hover {"
        "  background-color: #C4B398; color: #26394B;"
        "}"
        "QWidget#RigorRpcConsole QPushButton:pressed, QWidget#RigorRpcConsole QToolButton:pressed {"
        "  background-color: #B8A486; color: #26394B;"
        "}"
        "QWidget#RigorRpcConsole QPushButton:disabled { color: #7A8794; border: 1px solid #C4B49C; background: #E4D5BE; }"
        "QWidget#RigorRpcConsole QScrollBar:vertical { background: #E3D1B8; width: 12px; }"
        "QWidget#RigorRpcConsole QScrollBar::handle:vertical { background: #8A7E6C; min-height: 20px; border-radius: 4px; }"
    );
    if (rpcConsole) {
        rpcConsole->setObjectName(QStringLiteral("RigorRpcConsole"));
        rpcConsole->setStyleSheet(rpc_ss);
    }
}

} // namespace GUIUtil
