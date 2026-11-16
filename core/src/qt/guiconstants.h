// Copyright (c) 2011-2022 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_GUICONSTANTS_H
#define BITCOIN_QT_GUICONSTANTS_H

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;

/* A delay between model updates */
static constexpr auto MODEL_UPDATE_DELAY{250ms};

/* Coalesce incoming-credit tray balloons (gui#853-style). Solo mining
   otherwise fires one Windows toast per coinbase. */
static constexpr auto INCOMING_TX_NOTIFY_DELAY{2s};

/* A delay between shutdown pollings */
static constexpr auto SHUTDOWN_POLLING_DELAY{200ms};

/* AskPassphraseDialog -- Maximum passphrase length */
static const int MAX_PASSPHRASE_SIZE = 1024;

/* RigorGUI -- Size of icons in status bar */
static const int STATUSBAR_ICONSIZE = 16;

static const bool DEFAULT_SPLASHSCREEN = true;

/* Input fields: warm paper, blue-black ink. Invalid uses the restrained warning border. */
#define STYLE_INPUT_BACKGROUND "#F3E6D0"
#define STYLE_INPUT_FOREGROUND "#26394B"
#define STYLE_INVALID "background-color:" STYLE_INPUT_BACKGROUND "; color:" STYLE_INPUT_FOREGROUND "; border: 2px solid #6A3018;"

/* Transaction list -- unconfirmed transaction */
#define COLOR_UNCONFIRMED QColor(82, 100, 119)
/* Transaction list -- negative amount */
#define COLOR_NEGATIVE QColor(106, 48, 24)
/* Transaction list -- bare address (without label) */
#define COLOR_BAREADDRESS QColor(82, 100, 119)
/* Transaction list -- TX status decoration - danger, tx needs attention */
#define COLOR_TX_STATUS_DANGER QColor(106, 48, 24)
/* Transaction list -- TX status decoration - default color */
#define COLOR_BLACK QColor(38, 57, 75)

/* Tooltips longer than this (in characters) are converted into rich text,
   so that they can be word-wrapped.
 */
static const int TOOLTIP_WRAP_THRESHOLD = 80;

/* Number of frames in spinner animation */
#define SPINNER_FRAMES 36

#define QAPP_ORG_NAME "RIGOR"
#define QAPP_ORG_DOMAIN "rigor.org"
#define QAPP_APP_NAME_DEFAULT "RIGOR-Wallet"
#define QAPP_APP_NAME_TESTNET "RIGOR-Wallet-testnet"
#define QAPP_APP_NAME_SIGNET "RIGOR-Wallet-signet"
#define QAPP_APP_NAME_REGTEST "RIGOR-Wallet-regtest"

/* One gigabyte (GB) in bytes */
static constexpr uint64_t GB_BYTES{1000000000};

// Default prune target displayed in GUI.
static constexpr int DEFAULT_PRUNE_TARGET_GB{2};

#endif // BITCOIN_QT_GUICONSTANTS_H
