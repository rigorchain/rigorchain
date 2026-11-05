// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TOR_MODE_H
#define BITCOIN_TOR_MODE_H

/** How the existing Bitcoin Core 25 proxy / onion / listen flags are combined.
 *  This does not add a new network stack. It names the configurations those
 *  flags already implement.
 */
enum class TorNetworkMode {
    Normal = 0,
    OutboundOnly = 1,
    InboundOutbound = 2,
    Dual = 3,
    TorOnly = 4,
};

struct TorModeInputs {
    bool proxy_set{false};
    bool separate_onion_proxy{false};
    bool listen{false};
    bool listen_onion{false};
    bool onlynet_onion_only{false};
    bool bind_is_onion_only{false};
};

/** Saved settings, before Bitcoin Core's -proxy implies -listen=0 interaction. */
struct TorSettingsPresence {
    bool proxy_set{false};
    bool separate_onion_proxy{false};
    bool listen_present{false};
    bool listen_value{false};
    bool listen_onion_present{false};
    bool listen_onion_value{false};
    bool onlynet_onion_only{false};
    bool bind_is_onion_only{false};
};

TorNetworkMode InferTorNetworkMode(const TorModeInputs& in);
TorModeInputs ResolveTorModeInputs(const TorSettingsPresence& saved);

#endif // BITCOIN_TOR_MODE_H
