// Copyright (c) 2026 The RIGOR developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RIGOR_NET_DNS_SEED_H
#define RIGOR_NET_DNS_SEED_H

#include <netaddress.h>
#include <protocol.h>

#include <vector>

/** How a DNS seed name contributed peer addresses. Not consensus. */
enum class DnsSeedLookupKind {
    SERVICE_FILTERED, //!< x<flags>.seed returned addresses (Bitcoin seeder)
    BARE_NAME,        //!< seed hostname A/AAAA records, bound to the chain P2P port
    ADDR_FETCH,       //!< no addresses; caller may connect to the seed host once
};

struct DnsSeedImport {
    DnsSeedLookupKind kind{DnsSeedLookupKind::ADDR_FETCH};
    std::vector<CAddress> addresses;
};

/**
 * Choose peer addresses from a DNS seed lookup.
 *
 * Prefer the service-bit subdomain. If it has no addresses, use A/AAAA on
 * the seed hostname itself so a static zone can list current nodes.
 * Neither result yields ADDR_FETCH. Port is the chain P2P port, never RPC.
 */
DnsSeedImport ResolveDnsSeedImport(const std::vector<CNetAddr>& filtered_ips,
                                   const std::vector<CNetAddr>& bare_ips,
                                   uint16_t port,
                                   ServiceFlags services);

#endif // RIGOR_NET_DNS_SEED_H
