// Copyright (c) 2011-2020 The Bitcoin Core developers
// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAPPORT_H
#define BITCOIN_MAPPORT_H

static constexpr bool DEFAULT_UPNP = false;

static constexpr bool DEFAULT_NATPMP = false;

/** UPnP, NAT-PMP, and PCP are not implemented in this release. */
bool AutomaticPortMappingAvailable();

/** No mapping thread is started in this release. */
bool MapPortThreadActive();

void StartMapPort(bool use_upnp, bool use_natpmp);
void InterruptMapPort();
void StopMapPort();

#endif // BITCOIN_MAPPORT_H
