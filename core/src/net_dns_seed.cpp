// Copyright (c) 2026 The RIGOR developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net_dns_seed.h>

DnsSeedImport ResolveDnsSeedImport(const std::vector<CNetAddr>& filtered_ips,
                                   const std::vector<CNetAddr>& bare_ips,
                                   uint16_t port,
                                   ServiceFlags services)
{
    const std::vector<CNetAddr>* chosen = nullptr;
    DnsSeedLookupKind kind = DnsSeedLookupKind::ADDR_FETCH;
    if (!filtered_ips.empty()) {
        chosen = &filtered_ips;
        kind = DnsSeedLookupKind::SERVICE_FILTERED;
    } else if (!bare_ips.empty()) {
        chosen = &bare_ips;
        kind = DnsSeedLookupKind::BARE_NAME;
    }

    DnsSeedImport out;
    out.kind = kind;
    if (chosen == nullptr) return out;

    out.addresses.reserve(chosen->size());
    for (const CNetAddr& ip : *chosen) {
        if (!ip.IsValid() || ip.IsInternal()) continue;
        out.addresses.emplace_back(CService(ip, port), services);
    }
    if (out.addresses.empty()) {
        out.kind = DnsSeedLookupKind::ADDR_FETCH;
    }
    return out;
}
