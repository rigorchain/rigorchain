// Copyright (c) 2011-2022 The Bitcoin Core developers
// Copyright (c) 2026 The Rigor Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/rigor-config.h>
#endif

#include <mapport.h>

#include <logging.h>

bool AutomaticPortMappingAvailable()
{
    return false;
}

bool MapPortThreadActive()
{
    return false;
}

void StartMapPort(bool use_upnp, bool use_natpmp)
{
    // Legacy options stay recognized so an old configuration can still start.
    // They do not discover a gateway or send a mapping request.
    if (use_upnp || use_natpmp) {
        LogPrintf("Automatic port mapping is disabled in this release. -upnp and -natpmp are ignored. No UPnP, NAT-PMP, or PCP discovery or mapping is performed. Forward the public P2P port on the router manually if inbound connections are required.\n");
    }
}

void InterruptMapPort()
{
}

void StopMapPort()
{
}
