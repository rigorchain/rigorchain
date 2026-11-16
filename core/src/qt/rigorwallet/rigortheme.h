// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_RIGORWALLET_RIGORTHEME_H
#define BITCOIN_QT_RIGORWALLET_RIGORTHEME_H

#include <QColor>
#include <QString>

namespace RigorWallet {

/** Unified parchment notebook. Primary text is blue-black ballpoint ink. */
struct ColorPalette {
    QColor bgPrimary;       // #E3D1B8 workspace paper
    QColor bgSurface;       // #EADCC6 card paper
    QColor bgElevated;      // #EADCC6 card paper
    QColor borderSubtle;    // #8A7E6C
    QColor borderStrong;    // #6E6456
    QColor textPrimary;     // #26394B
    QColor textSecondary;   // #526477
    QColor textMuted;       // #526477
    QColor textDisabled;    // #7A8794
    QColor accent;          // #6A3018
    QColor accentHover;     // #542614
    QColor link;            // #3E5568
    QColor success;         // #2E4A32
    QColor warning;         // #6A3018
    QColor critical;        // #6A3018
    QColor info;            // #3E5568
    QColor experimental;    // #C4B49C
    QColor assetOru;        // #3E5568
    QColor assetOruBg;      // #3E556814
    QColor assetRes;        // #3E6244
    QColor assetResBg;      // #3E624414
    QColor convergence;     // #3E5568
    QColor convergenceBg;   // #3E556814
};

/** Typography scale from PROGRAM-005 RIGOR_VISUAL_LANGUAGE. */
struct Typography {
    int uiBodyPx;           // 14
    int uiBodyLargePx;      // 16
    int headingPx;          // 20
    int dataMonoPx;         // 13
    int balancePx;          // 24
    int labelPx;            // 12
    int statusEnumPx;       // 12
    QString uiFontFamily;
    QString monoFontFamily;
};

/** Layout spacing from 8 px base grid. */
struct Spacing {
    int unit;               // 8
    int panelPadding;       // 12
    int cardPadding;        // 16
    int sectionGap;         // 16
    int bannerPadding;      // 10
    int borderRadiusPanel;  // 6
    int borderRadiusButton; // 6
    int borderRadiusChip;   // 4
    int assetBorderWidth;   // 3
};

/** Theme accessor — returns canonical dark-mode tokens. */
class RigorTheme
{
public:
    RigorTheme() = delete;

    static ColorPalette colors();
    static Typography typography();
    static Spacing spacing();

    /** Stylesheet fragment for a given hex background + foreground. */
    static QString bannerStylesheet(const QColor& bg, const QColor& fg, const QColor& border);

    static QString colorHex(const QColor& color);
};

} // namespace RigorWallet

#endif // BITCOIN_QT_RIGORWALLET_RIGORTHEME_H
