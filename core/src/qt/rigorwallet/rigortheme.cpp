// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/rigorwallet/rigortheme.h>

namespace RigorWallet {

static QColor withAlpha(const QColor& base, int alpha)
{
    QColor c = base;
    c.setAlpha(alpha);
    return c;
}

ColorPalette RigorTheme::colors()
{
    ColorPalette p;
    p.bgPrimary = QColor(QStringLiteral("#E3D1B8"));
    p.bgSurface = QColor(QStringLiteral("#EADCC6"));
    p.bgElevated = QColor(QStringLiteral("#EADCC6"));
    p.borderSubtle = QColor(QStringLiteral("#8A7E6C"));
    p.borderStrong = QColor(QStringLiteral("#6E6456"));
    p.textPrimary = QColor(QStringLiteral("#26394B"));
    p.textSecondary = QColor(QStringLiteral("#526477"));
    p.textMuted = QColor(QStringLiteral("#526477"));
    p.textDisabled = QColor(QStringLiteral("#7A8794"));
    p.accent = QColor(QStringLiteral("#6A3018"));
    p.accentHover = QColor(QStringLiteral("#542614"));
    p.link = QColor(QStringLiteral("#3E5568"));
    p.success = QColor(QStringLiteral("#2E4A32"));
    p.warning = QColor(QStringLiteral("#6A3018"));
    p.critical = QColor(QStringLiteral("#6A3018"));
    p.info = QColor(QStringLiteral("#3E5568"));
    p.experimental = QColor(QStringLiteral("#C4B49C"));
    p.assetOru = QColor(QStringLiteral("#3E5568"));
    p.assetOruBg = withAlpha(p.assetOru, 0x14);
    p.assetRes = QColor(QStringLiteral("#3E6244"));
    p.assetResBg = withAlpha(p.assetRes, 0x14);
    p.convergence = QColor(QStringLiteral("#3E5568"));
    p.convergenceBg = withAlpha(p.convergence, 0x14);
    return p;
}

Typography RigorTheme::typography()
{
    Typography t;
    t.uiBodyPx = 14;
    t.uiBodyLargePx = 16;
    t.headingPx = 20;
    t.dataMonoPx = 13;
    t.balancePx = 24;
    t.labelPx = 12;
    t.statusEnumPx = 12;
    t.uiFontFamily = QStringLiteral(
        "Segoe UI, Microsoft YaHei UI, Microsoft YaHei, Noto Sans SC, "
        "Noto Sans CJK SC, WenQuanYi Micro Hei, Ubuntu, Cantarell, sans-serif");
    t.monoFontFamily = QStringLiteral("Roboto Mono, Consolas, monospace");
    return t;
}

Spacing RigorTheme::spacing()
{
    Spacing s;
    s.unit = 8;
    s.panelPadding = 12;
    s.cardPadding = 16;
    s.sectionGap = 16;
    s.bannerPadding = 10;
    s.borderRadiusPanel = 6;
    s.borderRadiusButton = 6;
    s.borderRadiusChip = 4;
    s.assetBorderWidth = 3;
    return s;
}

QString RigorTheme::colorHex(const QColor& color)
{
    return color.name(QColor::HexRgb);
}

QString RigorTheme::bannerStylesheet(const QColor& bg, const QColor& fg, const QColor& border)
{
    const Spacing s = spacing();
    return QStringLiteral(
        "QFrame#RigorBanner {"
        " background-color: %1;"
        " color: %2;"
        " border: 1px solid %3;"
        " border-radius: %4px;"
        " padding: %5px;"
        "}"
        " QLabel { background: transparent; color: %2; }")
        .arg(colorHex(bg), colorHex(fg), colorHex(border))
        .arg(s.borderRadiusPanel)
        .arg(s.bannerPadding);
}

} // namespace RigorWallet
