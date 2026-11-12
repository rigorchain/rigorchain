// Copyright (c) 2012-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <mapport.h>
#include <clientversion.h>
#include <compat/compat.h>
#include <cstdint>
#include <chainparamsbase.h>
#include <net.h>
#include <net_dns_seed.h>
#include <net_processing.h>
#include <netaddress.h>
#include <netbase.h>
#include <netmessagemaker.h>
#include <rigor_capability.h>
#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <test/util/net.h>
#include <test/util/setup_common.h>
#include <test/util/validation.h>
#include <timedata.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <version.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstring>
#include <ios>
#include <memory>
#include <optional>
#include <string>

using namespace std::literals;

BOOST_FIXTURE_TEST_SUITE(net_tests, RegTestingSetup)

BOOST_AUTO_TEST_CASE(paused_send_disconnects_after_timeout)
{
    struct RestoreMockTime {
        ~RestoreMockTime() { SetMockTime(0s); }
    } restore_mock_time;

    // Protocol version 209 predates BIP31 nonce pings. RIGOR always sends a
    // nonce and does not disconnect a peer for lacking that feature. A peer
    // that stays send-paused is disconnected after TIMEOUT_INTERVAL even when
    // partial sends keep m_last_send fresh.
    const auto start = std::chrono::seconds{1'700'000'000};
    SetMockTime(start);

    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    connman.SetPeerConnectTimeout(1s);

    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;
    CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    CNode legacy{/*id=*/1,
                 /*sock=*/nullptr,
                 addr,
                 /*nKeyedNetGroupIn=*/0,
                 /*nLocalHostNonceIn=*/0,
                 CAddress{},
                 /*addrNameIn=*/std::string{},
                 ConnectionType::OUTBOUND_FULL_RELAY,
                 /*inbound_onion=*/false};
    legacy.nVersion = 209;
    legacy.fSuccessfullyConnected = true;
    legacy.m_last_send = start;
    legacy.m_last_recv = start;

    CNode healthy{/*id=*/2,
                  /*sock=*/nullptr,
                  addr,
                  /*nKeyedNetGroupIn=*/1,
                  /*nLocalHostNonceIn=*/1,
                  CAddress{},
                  /*addrNameIn=*/std::string{},
                  ConnectionType::OUTBOUND_FULL_RELAY,
                  /*inbound_onion=*/false};
    healthy.nVersion = PROTOCOL_VERSION;
    healthy.fSuccessfullyConnected = true;
    healthy.m_last_send = start;
    healthy.m_last_recv = start;

    SetMockTime(start + 2s);
    BOOST_CHECK(!connman.InactivityCheck(legacy));

    legacy.SetSendPaused(true);
    const auto pause_started = legacy.m_pause_send_since.load();
    BOOST_CHECK_EQUAL(pause_started.count(), (start + 2s).count());
    legacy.SetSendPaused(true);
    BOOST_CHECK_EQUAL(legacy.m_pause_send_since.load().count(), pause_started.count());

    legacy.m_last_send = start + 2s;
    legacy.m_last_recv = start + 2s;
    BOOST_CHECK(legacy.fPauseSend);
    BOOST_CHECK(!connman.InactivityCheck(legacy));

    SetMockTime(pause_started + TIMEOUT_INTERVAL);
    legacy.m_last_send = pause_started + TIMEOUT_INTERVAL;
    legacy.m_last_recv = pause_started + TIMEOUT_INTERVAL;
    BOOST_CHECK(!connman.InactivityCheck(legacy));

    SetMockTime(pause_started + TIMEOUT_INTERVAL + 1s);
    legacy.m_last_send = pause_started + TIMEOUT_INTERVAL + 1s;
    legacy.m_last_recv = pause_started + TIMEOUT_INTERVAL + 1s;
    BOOST_CHECK(connman.InactivityCheck(legacy));

    legacy.SetSendPaused(false);
    BOOST_CHECK(!legacy.fPauseSend);
    BOOST_CHECK(!connman.InactivityCheck(legacy));

    const auto resumed = pause_started + TIMEOUT_INTERVAL + 1s;
    legacy.SetSendPaused(true);
    BOOST_CHECK_EQUAL(legacy.m_pause_send_since.load().count(), resumed.count());
    legacy.m_last_send = resumed;
    legacy.m_last_recv = resumed;
    BOOST_CHECK(!connman.InactivityCheck(legacy));

    legacy.SetSendPaused(false);
    SetMockTime(resumed + TIMEOUT_INTERVAL + 1s);
    legacy.m_last_send = resumed + TIMEOUT_INTERVAL + 1s;
    legacy.m_last_recv = resumed + TIMEOUT_INTERVAL + 1s;
    BOOST_CHECK(!connman.InactivityCheck(legacy));

    legacy.m_last_send = resumed;
    BOOST_CHECK(connman.InactivityCheck(legacy));

    const auto now = GetTime<std::chrono::seconds>();
    healthy.m_last_send = now;
    healthy.m_last_recv = now;
    BOOST_CHECK(!healthy.fPauseSend);
    BOOST_CHECK(!connman.InactivityCheck(healthy));
    healthy.SetSendPaused(true);
    BOOST_CHECK(!connman.InactivityCheck(healthy));
    healthy.SetSendPaused(false);
    BOOST_CHECK(!connman.InactivityCheck(healthy));
}

BOOST_AUTO_TEST_CASE(paused_peer_tx_announcements_are_bounded)
{
    struct RestoreMockTime {
        ~RestoreMockTime() { SetMockTime(0s); }
    } restore_mock_time;

    const auto start = std::chrono::seconds{1'700'000'000};
    SetMockTime(start);

    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    connman.SetPeerConnectTimeout(1s);
    auto& peerman = *m_node.peerman;

    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;
    const CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);

    CNode paused{/*id=*/9101,
                 /*sock=*/nullptr,
                 addr,
                 /*nKeyedNetGroupIn=*/0,
                 /*nLocalHostNonceIn=*/0,
                 CAddress{},
                 /*addrNameIn=*/std::string{},
                 ConnectionType::OUTBOUND_FULL_RELAY,
                 /*inbound_onion=*/false};
    CNode healthy{/*id=*/9102,
                  /*sock=*/nullptr,
                  addr,
                  /*nKeyedNetGroupIn=*/1,
                  /*nLocalHostNonceIn=*/1,
                  CAddress{},
                  /*addrNameIn=*/std::string{},
                  ConnectionType::OUTBOUND_FULL_RELAY,
                  /*inbound_onion=*/false};

    struct Cleanup {
        ConnmanTestMsg& connman;
        PeerManager& peerman;
        CNode& paused;
        CNode& healthy;
        bool finalized{false};
        ~Cleanup()
        {
            connman.RemoveTestNode(paused);
            connman.RemoveTestNode(healthy);
            if (!finalized) {
                peerman.FinalizeNode(paused);
                peerman.FinalizeNode(healthy);
            }
        }
    } cleanup{connman, peerman, paused, healthy};

    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        const ServiceFlags services{NODE_NETWORK | NODE_WITNESS};
        connman.Handshake(paused, /*successfully_connected=*/true, services, services, RIGOR_PROTOCOL_CURRENT_VERSION, /*relay_txs=*/true);
        connman.Handshake(healthy, /*successfully_connected=*/true, services, services, RIGOR_PROTOCOL_CURRENT_VERSION, /*relay_txs=*/true);
    }
    BOOST_REQUIRE(paused.fSuccessfullyConnected);
    BOOST_REQUIRE(healthy.fSuccessfullyConnected);
    // A zero test send buffer pauses on the handshake's own messages.
    // The case below pauses one peer on purpose.
    paused.SetSendPaused(false);
    healthy.SetSendPaused(false);
    connman.AddTestNode(paused);
    connman.AddTestNode(healthy);

    const auto send_bytes = [](CNode& node) {
        LOCK(node.cs_vSend);
        return node.nSendSize;
    };
    const auto announce = [&](uint32_t n) {
        uint256 hash;
        std::memcpy(hash.begin(), &n, sizeof(n));
        peerman.RelayTransaction(hash, hash);
    };

    for (uint32_t i = 1; i <= 10; ++i) {
        announce(i);
    }
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), 10U);
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(healthy.GetId()), 10U);

    paused.SetSendPaused(true);
    const size_t paused_bytes = send_bytes(paused);
    for (uint32_t i = 11; i <= 50; ++i) {
        announce(i);
    }
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), static_cast<unsigned>(PAUSED_TX_INVENTORY_LIMIT));
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(healthy.GetId()), 50U);
    announce(1);
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), static_cast<unsigned>(PAUSED_TX_INVENTORY_LIMIT));
    announce(36);
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), static_cast<unsigned>(PAUSED_TX_INVENTORY_LIMIT));
    paused.SetSendPaused(false);
    announce(36);
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), static_cast<unsigned>(PAUSED_TX_INVENTORY_LIMIT) + 1U);
    paused.SetSendPaused(true);
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(healthy.GetId()), 50U);

    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        BOOST_CHECK(peerman.SendMessages(&paused));
    }
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), static_cast<unsigned>(PAUSED_TX_INVENTORY_LIMIT));
    BOOST_CHECK(send_bytes(paused) - paused_bytes < 1000);
    BOOST_CHECK(send_bytes(paused) >= paused_bytes);

    const size_t before_ping = send_bytes(paused);
    connman.PushMessage(&paused, CNetMsgMaker{PROTOCOL_VERSION}.Make(NetMsgType::PING, uint64_t{9}));
    BOOST_CHECK(send_bytes(paused) > before_ping);
    BOOST_CHECK(paused.fPauseSend);
    BOOST_CHECK(!paused.fDisconnect);

    paused.SetSendPaused(false);
    BOOST_CHECK(!paused.fPauseSend);
    for (uint32_t i = 51; i <= 60; ++i) {
        announce(i);
    }
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), static_cast<unsigned>(PAUSED_TX_INVENTORY_LIMIT) + 10U);
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(healthy.GetId()), 60U);

    paused.SetSendPaused(true);
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        BOOST_CHECK(peerman.SendMessages(&paused));
    }
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), static_cast<unsigned>(PAUSED_TX_INVENTORY_LIMIT));
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(healthy.GetId()), 60U);

    const auto pause_started = paused.m_pause_send_since.load();
    paused.SetSendPaused(true);
    BOOST_CHECK_EQUAL(paused.m_pause_send_since.load().count(), pause_started.count());

    SetMockTime(pause_started + TIMEOUT_INTERVAL);
    paused.m_last_send = pause_started + TIMEOUT_INTERVAL;
    paused.m_last_recv = pause_started + TIMEOUT_INTERVAL;
    BOOST_CHECK(!connman.InactivityCheck(paused));

    SetMockTime(pause_started + TIMEOUT_INTERVAL + 1s);
    paused.m_last_send = pause_started + TIMEOUT_INTERVAL + 1s;
    paused.m_last_recv = pause_started + TIMEOUT_INTERVAL + 1s;
    BOOST_CHECK(connman.InactivityCheck(paused));

    const auto now = GetTime<std::chrono::seconds>();
    healthy.m_last_send = now;
    healthy.m_last_recv = now;
    BOOST_CHECK(!healthy.fPauseSend);
    BOOST_CHECK(!connman.InactivityCheck(healthy));

    peerman.FinalizeNode(paused);
    peerman.FinalizeNode(healthy);
    cleanup.finalized = true;
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(paused.GetId()), 0U);
    BOOST_CHECK_EQUAL(peerman.GetTxAnnouncementCount(healthy.GetId()), 0U);
}

BOOST_AUTO_TEST_CASE(paused_send_buffer_keeps_control_traffic)
{
    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;
    const CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    CNode node{/*id=*/9201,
               /*sock=*/nullptr,
               addr,
               /*nKeyedNetGroupIn=*/0,
               /*nLocalHostNonceIn=*/0,
               CAddress{},
               /*addrNameIn=*/std::string{},
               ConnectionType::OUTBOUND_FULL_RELAY,
               /*inbound_onion=*/false};
    node.fSuccessfullyConnected = true;
    node.SetSendPaused(true);
    const size_t pause_limit = connman.TestSendBufferMax();
    {
        LOCK(node.cs_vSend);
        node.nSendSize = pause_limit + 1;
    }
    const auto queued = [&]() {
        LOCK(node.cs_vSend);
        return node.nSendSize;
    };
    const size_t saturated = queued();
    CNetMsgMaker maker{PROTOCOL_VERSION};
    BOOST_CHECK(!connman.PushMessage(&node, maker.Make(NetMsgType::INV, std::vector<CInv>{})));
    BOOST_CHECK_EQUAL(queued(), saturated);
    BOOST_CHECK(connman.PushMessage(&node, maker.Make(NetMsgType::PING, uint64_t{11})));
    BOOST_CHECK(queued() > saturated);
    BOOST_CHECK(node.fPauseSend);
    BOOST_CHECK(!node.fDisconnect);
    {
        LOCK(node.cs_vSend);
        node.nSendSize = pause_limit + PAUSED_SEND_CONTROL_HEADROOM + 1;
    }
    const size_t capped = queued();
    BOOST_CHECK(!connman.PushMessage(&node, maker.Make(NetMsgType::PING, uint64_t{12})));
    BOOST_CHECK_EQUAL(queued(), capped);
    BOOST_CHECK(!connman.PushMessage(&node, maker.Make(NetMsgType::BLOCK, CBlock{})));
    BOOST_CHECK_EQUAL(queued(), capped);
}

BOOST_AUTO_TEST_CASE(paused_block_announcements_stay_bounded)
{
    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    auto& peerman = *m_node.peerman;
    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;
    const CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    CNode paused{/*id=*/9301,
                 /*sock=*/nullptr,
                 addr,
                 /*nKeyedNetGroupIn=*/0,
                 /*nLocalHostNonceIn=*/0,
                 CAddress{},
                 /*addrNameIn=*/std::string{},
                 ConnectionType::OUTBOUND_FULL_RELAY,
                 /*inbound_onion=*/false};
    CNode healthy{/*id=*/9302,
                  /*sock=*/nullptr,
                  addr,
                  /*nKeyedNetGroupIn=*/1,
                  /*nLocalHostNonceIn=*/1,
                  CAddress{},
                  /*addrNameIn=*/std::string{},
                  ConnectionType::OUTBOUND_FULL_RELAY,
                  /*inbound_onion=*/false};
    struct Cleanup {
        ConnmanTestMsg& connman;
        PeerManager& peerman;
        CNode& paused;
        CNode& healthy;
        bool finalized{false};
        ~Cleanup()
        {
            connman.RemoveTestNode(paused);
            connman.RemoveTestNode(healthy);
            if (!finalized) {
                peerman.FinalizeNode(paused);
                peerman.FinalizeNode(healthy);
            }
        }
    } cleanup{connman, peerman, paused, healthy};
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        const ServiceFlags services{NODE_NETWORK | NODE_WITNESS};
        connman.Handshake(paused, true, services, services, RIGOR_PROTOCOL_CURRENT_VERSION, true);
        connman.Handshake(healthy, true, services, services, RIGOR_PROTOCOL_CURRENT_VERSION, true);
    }
    connman.AddTestNode(paused);
    connman.AddTestNode(healthy);
    paused.SetSendPaused(false);
    healthy.SetSendPaused(false);
    paused.SetSendPaused(true);

    const CBlockIndex* tip = WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip());
    BOOST_REQUIRE(tip != nullptr);
    peerman.TestAnnounceBlockTip(tip);
    const size_t one = peerman.GetBlockAnnouncementCount(healthy.GetId());
    BOOST_REQUIRE(one >= 1U);
    BOOST_REQUIRE(one <= 8U);
    for (int i = 1; i < 20; ++i) {
        peerman.TestAnnounceBlockTip(tip);
    }
    const size_t paused_before = peerman.GetBlockAnnouncementCount(paused.GetId());
    const size_t healthy_before = peerman.GetBlockAnnouncementCount(healthy.GetId());
    BOOST_CHECK_EQUAL(paused_before, std::min<size_t>(one * 20, 8));
    BOOST_CHECK_EQUAL(healthy_before, one * 20);
    BOOST_CHECK(healthy_before > paused_before);
    BOOST_CHECK(peerman.GetNewestBlockAnnouncement(paused.GetId()) == tip->GetBlockHash());
    const auto paused_bytes = WITH_LOCK(paused.cs_vSend, return paused.nSendSize);
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        BOOST_CHECK(peerman.SendMessages(&paused));
    }
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(paused.GetId()), paused_before);
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(healthy.GetId()), healthy_before);
    BOOST_CHECK_EQUAL(WITH_LOCK(paused.cs_vSend, return paused.nSendSize), paused_bytes);

    peerman.FinalizeNode(paused);
    peerman.FinalizeNode(healthy);
    cleanup.finalized = true;
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(paused.GetId()), 0U);
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(healthy.GetId()), 0U);
}

BOOST_AUTO_TEST_CASE(cnode_listen_port)
{
    // test default
    uint16_t port{GetListenPort()};
    BOOST_CHECK(port == Params().GetDefaultPort());
    // test set port
    uint16_t altPort = 12345;
    BOOST_CHECK(gArgs.SoftSetArg("-port", ToString(altPort)));
    port = GetListenPort();
    BOOST_CHECK(port == altPort);
}

BOOST_AUTO_TEST_CASE(cnode_simple_test)
{
    NodeId id = 0;

    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;

    CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    std::string pszDest;

    std::unique_ptr<CNode> pnode1 = std::make_unique<CNode>(id++,
                                                            /*sock=*/nullptr,
                                                            addr,
                                                            /*nKeyedNetGroupIn=*/0,
                                                            /*nLocalHostNonceIn=*/0,
                                                            CAddress(),
                                                            pszDest,
                                                            ConnectionType::OUTBOUND_FULL_RELAY,
                                                            /*inbound_onion=*/false);
    BOOST_CHECK(pnode1->IsFullOutboundConn() == true);
    BOOST_CHECK(pnode1->IsManualConn() == false);
    BOOST_CHECK(pnode1->IsBlockOnlyConn() == false);
    BOOST_CHECK(pnode1->IsFeelerConn() == false);
    BOOST_CHECK(pnode1->IsAddrFetchConn() == false);
    BOOST_CHECK(pnode1->IsInboundConn() == false);
    BOOST_CHECK(pnode1->m_inbound_onion == false);
    BOOST_CHECK_EQUAL(pnode1->ConnectedThroughNetwork(), Network::NET_IPV4);

    std::unique_ptr<CNode> pnode2 = std::make_unique<CNode>(id++,
                                                            /*sock=*/nullptr,
                                                            addr,
                                                            /*nKeyedNetGroupIn=*/1,
                                                            /*nLocalHostNonceIn=*/1,
                                                            CAddress(),
                                                            pszDest,
                                                            ConnectionType::INBOUND,
                                                            /*inbound_onion=*/false);
    BOOST_CHECK(pnode2->IsFullOutboundConn() == false);
    BOOST_CHECK(pnode2->IsManualConn() == false);
    BOOST_CHECK(pnode2->IsBlockOnlyConn() == false);
    BOOST_CHECK(pnode2->IsFeelerConn() == false);
    BOOST_CHECK(pnode2->IsAddrFetchConn() == false);
    BOOST_CHECK(pnode2->IsInboundConn() == true);
    BOOST_CHECK(pnode2->m_inbound_onion == false);
    BOOST_CHECK_EQUAL(pnode2->ConnectedThroughNetwork(), Network::NET_IPV4);

    std::unique_ptr<CNode> pnode3 = std::make_unique<CNode>(id++,
                                                            /*sock=*/nullptr,
                                                            addr,
                                                            /*nKeyedNetGroupIn=*/0,
                                                            /*nLocalHostNonceIn=*/0,
                                                            CAddress(),
                                                            pszDest,
                                                            ConnectionType::OUTBOUND_FULL_RELAY,
                                                            /*inbound_onion=*/false);
    BOOST_CHECK(pnode3->IsFullOutboundConn() == true);
    BOOST_CHECK(pnode3->IsManualConn() == false);
    BOOST_CHECK(pnode3->IsBlockOnlyConn() == false);
    BOOST_CHECK(pnode3->IsFeelerConn() == false);
    BOOST_CHECK(pnode3->IsAddrFetchConn() == false);
    BOOST_CHECK(pnode3->IsInboundConn() == false);
    BOOST_CHECK(pnode3->m_inbound_onion == false);
    BOOST_CHECK_EQUAL(pnode3->ConnectedThroughNetwork(), Network::NET_IPV4);

    std::unique_ptr<CNode> pnode4 = std::make_unique<CNode>(id++,
                                                            /*sock=*/nullptr,
                                                            addr,
                                                            /*nKeyedNetGroupIn=*/1,
                                                            /*nLocalHostNonceIn=*/1,
                                                            CAddress(),
                                                            pszDest,
                                                            ConnectionType::INBOUND,
                                                            /*inbound_onion=*/true);
    BOOST_CHECK(pnode4->IsFullOutboundConn() == false);
    BOOST_CHECK(pnode4->IsManualConn() == false);
    BOOST_CHECK(pnode4->IsBlockOnlyConn() == false);
    BOOST_CHECK(pnode4->IsFeelerConn() == false);
    BOOST_CHECK(pnode4->IsAddrFetchConn() == false);
    BOOST_CHECK(pnode4->IsInboundConn() == true);
    BOOST_CHECK(pnode4->m_inbound_onion == true);
    BOOST_CHECK_EQUAL(pnode4->ConnectedThroughNetwork(), Network::NET_ONION);
}

BOOST_AUTO_TEST_CASE(cnetaddr_basic)
{
    CNetAddr addr;

    // IPv4, INADDR_ANY
    BOOST_REQUIRE(LookupHost("0.0.0.0", addr, false));
    BOOST_REQUIRE(!addr.IsValid());
    BOOST_REQUIRE(addr.IsIPv4());

    BOOST_CHECK(addr.IsBindAny());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "0.0.0.0");

    // IPv4, INADDR_NONE
    BOOST_REQUIRE(LookupHost("255.255.255.255", addr, false));
    BOOST_REQUIRE(!addr.IsValid());
    BOOST_REQUIRE(addr.IsIPv4());

    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "255.255.255.255");

    // IPv4, casual
    BOOST_REQUIRE(LookupHost("12.34.56.78", addr, false));
    BOOST_REQUIRE(addr.IsValid());
    BOOST_REQUIRE(addr.IsIPv4());

    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "12.34.56.78");

    // IPv6, in6addr_any
    BOOST_REQUIRE(LookupHost("::", addr, false));
    BOOST_REQUIRE(!addr.IsValid());
    BOOST_REQUIRE(addr.IsIPv6());

    BOOST_CHECK(addr.IsBindAny());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "::");

    // IPv6, casual
    BOOST_REQUIRE(LookupHost("1122:3344:5566:7788:9900:aabb:ccdd:eeff", addr, false));
    BOOST_REQUIRE(addr.IsValid());
    BOOST_REQUIRE(addr.IsIPv6());

    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "1122:3344:5566:7788:9900:aabb:ccdd:eeff");

    // IPv6, scoped/link-local. See https://tools.ietf.org/html/rfc4007
    // We support non-negative decimal integers (uint32_t) as zone id indices.
    // Normal link-local scoped address functionality is to append "%" plus the
    // zone id, for example, given a link-local address of "fe80::1" and a zone
    // id of "32", return the address as "fe80::1%32".
    const std::string link_local{"fe80::1"};
    const std::string scoped_addr{link_local + "%32"};
    BOOST_REQUIRE(LookupHost(scoped_addr, addr, false));
    BOOST_REQUIRE(addr.IsValid());
    BOOST_REQUIRE(addr.IsIPv6());
    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), scoped_addr);

    // Test that the delimiter "%" and default zone id of 0 can be omitted for the default scope.
    BOOST_REQUIRE(LookupHost(link_local + "%0", addr, false));
    BOOST_REQUIRE(addr.IsValid());
    BOOST_REQUIRE(addr.IsIPv6());
    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), link_local);

    // TORv2, no longer supported
    BOOST_CHECK(!addr.SetSpecial("6hzph5hv6337r6p2.onion"));

    // TORv3
    const char* torv3_addr = "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion";
    BOOST_REQUIRE(addr.SetSpecial(torv3_addr));
    BOOST_REQUIRE(addr.IsValid());
    BOOST_REQUIRE(addr.IsTor());

    BOOST_CHECK(!addr.IsI2P());
    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK(!addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), torv3_addr);

    // TORv3, broken, with wrong checksum
    BOOST_CHECK(!addr.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscsad.onion"));

    // TORv3, broken, with wrong version
    BOOST_CHECK(!addr.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscrye.onion"));

    // TORv3, malicious
    BOOST_CHECK(!addr.SetSpecial(std::string{
        "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd\0wtf.onion", 66}));

    // TOR, bogus length
    BOOST_CHECK(!addr.SetSpecial(std::string{"mfrggzak.onion"}));

    // TOR, invalid base32
    BOOST_CHECK(!addr.SetSpecial(std::string{"mf*g zak.onion"}));

    // I2P
    const char* i2p_addr = "UDHDrtrcetjm5sxzskjyr5ztpeszydbh4dpl3pl4utgqqw2v4jna.b32.I2P";
    BOOST_REQUIRE(addr.SetSpecial(i2p_addr));
    BOOST_REQUIRE(addr.IsValid());
    BOOST_REQUIRE(addr.IsI2P());

    BOOST_CHECK(!addr.IsTor());
    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK(!addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), ToLower(i2p_addr));

    // I2P, correct length, but decodes to less than the expected number of bytes.
    BOOST_CHECK(!addr.SetSpecial("udhdrtrcetjm5sxzskjyr5ztpeszydbh4dpl3pl4utgqqw2v4jn=.b32.i2p"));

    // I2P, extra unnecessary padding
    BOOST_CHECK(!addr.SetSpecial("udhdrtrcetjm5sxzskjyr5ztpeszydbh4dpl3pl4utgqqw2v4jna=.b32.i2p"));

    // I2P, malicious
    BOOST_CHECK(!addr.SetSpecial("udhdrtrcetjm5sxzskjyr5ztpeszydbh4dpl3pl4utgqqw2v\0wtf.b32.i2p"s));

    // I2P, valid but unsupported (56 Base32 characters)
    // See "Encrypted LS with Base 32 Addresses" in
    // https://geti2p.net/spec/encryptedleaseset.txt
    BOOST_CHECK(
        !addr.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscsad.b32.i2p"));

    // I2P, invalid base32
    BOOST_CHECK(!addr.SetSpecial(std::string{"tp*szydbh4dp.b32.i2p"}));

    // Internal
    addr.SetInternal("esffpp");
    BOOST_REQUIRE(!addr.IsValid()); // "internal" is considered invalid
    BOOST_REQUIRE(addr.IsInternal());

    BOOST_CHECK(!addr.IsBindAny());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "esffpvrt3wpeaygy.internal");

    // Totally bogus
    BOOST_CHECK(!addr.SetSpecial("totally bogus"));
}

BOOST_AUTO_TEST_CASE(cnetaddr_tostring_canonical_ipv6)
{
    // Test that CNetAddr::ToString formats IPv6 addresses with zero compression as described in
    // RFC 5952 ("A Recommendation for IPv6 Address Text Representation").
    const std::map<std::string, std::string> canonical_representations_ipv6{
        {"0000:0000:0000:0000:0000:0000:0000:0000", "::"},
        {"000:0000:000:00:0:00:000:0000", "::"},
        {"000:000:000:000:000:000:000:000", "::"},
        {"00:00:00:00:00:00:00:00", "::"},
        {"0:0:0:0:0:0:0:0", "::"},
        {"0:0:0:0:0:0:0:1", "::1"},
        {"2001:0:0:1:0:0:0:1", "2001:0:0:1::1"},
        {"2001:0db8:0:0:1:0:0:1", "2001:db8::1:0:0:1"},
        {"2001:0db8:85a3:0000:0000:8a2e:0370:7334", "2001:db8:85a3::8a2e:370:7334"},
        {"2001:0db8::0001", "2001:db8::1"},
        {"2001:0db8::0001:0000", "2001:db8::1:0"},
        {"2001:0db8::1:0:0:1", "2001:db8::1:0:0:1"},
        {"2001:db8:0000:0:1::1", "2001:db8::1:0:0:1"},
        {"2001:db8:0000:1:1:1:1:1", "2001:db8:0:1:1:1:1:1"},
        {"2001:db8:0:0:0:0:2:1", "2001:db8::2:1"},
        {"2001:db8:0:0:0::1", "2001:db8::1"},
        {"2001:db8:0:0:1:0:0:1", "2001:db8::1:0:0:1"},
        {"2001:db8:0:0:1::1", "2001:db8::1:0:0:1"},
        {"2001:DB8:0:0:1::1", "2001:db8::1:0:0:1"},
        {"2001:db8:0:0::1", "2001:db8::1"},
        {"2001:db8:0:0:aaaa::1", "2001:db8::aaaa:0:0:1"},
        {"2001:db8:0:1:1:1:1:1", "2001:db8:0:1:1:1:1:1"},
        {"2001:db8:0::1", "2001:db8::1"},
        {"2001:db8:85a3:0:0:8a2e:370:7334", "2001:db8:85a3::8a2e:370:7334"},
        {"2001:db8::0:1", "2001:db8::1"},
        {"2001:db8::0:1:0:0:1", "2001:db8::1:0:0:1"},
        {"2001:DB8::1", "2001:db8::1"},
        {"2001:db8::1", "2001:db8::1"},
        {"2001:db8::1:0:0:1", "2001:db8::1:0:0:1"},
        {"2001:db8::1:1:1:1:1", "2001:db8:0:1:1:1:1:1"},
        {"2001:db8::aaaa:0:0:1", "2001:db8::aaaa:0:0:1"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:0:1", "2001:db8:aaaa:bbbb:cccc:dddd:0:1"},
        {"2001:db8:aaaa:bbbb:cccc:dddd::1", "2001:db8:aaaa:bbbb:cccc:dddd:0:1"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:eeee:0001", "2001:db8:aaaa:bbbb:cccc:dddd:eeee:1"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:eeee:001", "2001:db8:aaaa:bbbb:cccc:dddd:eeee:1"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:eeee:01", "2001:db8:aaaa:bbbb:cccc:dddd:eeee:1"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:eeee:1", "2001:db8:aaaa:bbbb:cccc:dddd:eeee:1"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:eeee:aaaa", "2001:db8:aaaa:bbbb:cccc:dddd:eeee:aaaa"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:eeee:AAAA", "2001:db8:aaaa:bbbb:cccc:dddd:eeee:aaaa"},
        {"2001:db8:aaaa:bbbb:cccc:dddd:eeee:AaAa", "2001:db8:aaaa:bbbb:cccc:dddd:eeee:aaaa"},
    };
    for (const auto& [input_address, expected_canonical_representation_output] : canonical_representations_ipv6) {
        CNetAddr net_addr;
        BOOST_REQUIRE(LookupHost(input_address, net_addr, false));
        BOOST_REQUIRE(net_addr.IsIPv6());
        BOOST_CHECK_EQUAL(net_addr.ToStringAddr(), expected_canonical_representation_output);
    }
}

BOOST_AUTO_TEST_CASE(cnetaddr_serialize_v1)
{
    CNetAddr addr;
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);

    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "00000000000000000000000000000000");
    s.clear();

    BOOST_REQUIRE(LookupHost("1.2.3.4", addr, false));
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "00000000000000000000ffff01020304");
    s.clear();

    BOOST_REQUIRE(LookupHost("1a1b:2a2b:3a3b:4a4b:5a5b:6a6b:7a7b:8a8b", addr, false));
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "1a1b2a2b3a3b4a4b5a5b6a6b7a7b8a8b");
    s.clear();

    // TORv2, no longer supported
    BOOST_CHECK(!addr.SetSpecial("6hzph5hv6337r6p2.onion"));

    BOOST_REQUIRE(addr.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"));
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "00000000000000000000000000000000");
    s.clear();

    addr.SetInternal("a");
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "fd6b88c08724ca978112ca1bbdcafac2");
    s.clear();
}

BOOST_AUTO_TEST_CASE(cnetaddr_serialize_v2)
{
    CNetAddr addr;
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    // Add ADDRV2_FORMAT to the version so that the CNetAddr
    // serialize method produces an address in v2 format.
    s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);

    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "021000000000000000000000000000000000");
    s.clear();

    BOOST_REQUIRE(LookupHost("1.2.3.4", addr, false));
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "010401020304");
    s.clear();

    BOOST_REQUIRE(LookupHost("1a1b:2a2b:3a3b:4a4b:5a5b:6a6b:7a7b:8a8b", addr, false));
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "02101a1b2a2b3a3b4a4b5a5b6a6b7a7b8a8b");
    s.clear();

    // TORv2, no longer supported
    BOOST_CHECK(!addr.SetSpecial("6hzph5hv6337r6p2.onion"));

    BOOST_REQUIRE(addr.SetSpecial("kpgvmscirrdqpekbqjsvw5teanhatztpp2gl6eee4zkowvwfxwenqaid.onion"));
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "042053cd5648488c4707914182655b7664034e09e66f7e8cbf1084e654eb56c5bd88");
    s.clear();

    BOOST_REQUIRE(addr.SetInternal("a"));
    s << addr;
    BOOST_CHECK_EQUAL(HexStr(s), "0210fd6b88c08724ca978112ca1bbdcafac2");
    s.clear();
}

BOOST_AUTO_TEST_CASE(cnetaddr_unserialize_v2)
{
    CNetAddr addr;
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    // Add ADDRV2_FORMAT to the version so that the CNetAddr
    // unserialize method expects an address in v2 format.
    s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);

    // Valid IPv4.
    s << Span{ParseHex("01"          // network type (IPv4)
                       "04"          // address length
                       "01020304")}; // address
    s >> addr;
    BOOST_CHECK(addr.IsValid());
    BOOST_CHECK(addr.IsIPv4());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "1.2.3.4");
    BOOST_REQUIRE(s.empty());

    // Invalid IPv4, valid length but address itself is shorter.
    s << Span{ParseHex("01"      // network type (IPv4)
                       "04"      // address length
                       "0102")}; // address
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure, HasReason("end of data"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Invalid IPv4, with bogus length.
    s << Span{ParseHex("01"          // network type (IPv4)
                       "05"          // address length
                       "01020304")}; // address
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure,
                          HasReason("BIP155 IPv4 address with length 5 (should be 4)"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Invalid IPv4, with extreme length.
    s << Span{ParseHex("01"          // network type (IPv4)
                       "fd0102"      // address length (513 as CompactSize)
                       "01020304")}; // address
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure,
                          HasReason("Address too long: 513 > 512"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Valid IPv6.
    s << Span{ParseHex("02"                                  // network type (IPv6)
                       "10"                                  // address length
                       "0102030405060708090a0b0c0d0e0f10")}; // address
    s >> addr;
    BOOST_CHECK(addr.IsValid());
    BOOST_CHECK(addr.IsIPv6());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "102:304:506:708:90a:b0c:d0e:f10");
    BOOST_REQUIRE(s.empty());

    // Valid IPv6, contains embedded "internal".
    s << Span{ParseHex(
        "02"                                  // network type (IPv6)
        "10"                                  // address length
        "fd6b88c08724ca978112ca1bbdcafac2")}; // address: 0xfd + sha256("bitcoin")[0:5] +
                                              // sha256(name)[0:10]
    s >> addr;
    BOOST_CHECK(addr.IsInternal());
    BOOST_CHECK(addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "zklycewkdo64v6wc.internal");
    BOOST_REQUIRE(s.empty());

    // Invalid IPv6, with bogus length.
    s << Span{ParseHex("02"    // network type (IPv6)
                       "04"    // address length
                       "00")}; // address
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure,
                          HasReason("BIP155 IPv6 address with length 4 (should be 16)"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Invalid IPv6, contains embedded IPv4.
    s << Span{ParseHex("02"                                  // network type (IPv6)
                       "10"                                  // address length
                       "00000000000000000000ffff01020304")}; // address
    s >> addr;
    BOOST_CHECK(!addr.IsValid());
    BOOST_REQUIRE(s.empty());

    // Invalid IPv6, contains embedded TORv2.
    s << Span{ParseHex("02"                                  // network type (IPv6)
                       "10"                                  // address length
                       "fd87d87eeb430102030405060708090a")}; // address
    s >> addr;
    BOOST_CHECK(!addr.IsValid());
    BOOST_REQUIRE(s.empty());

    // TORv2, no longer supported.
    s << Span{ParseHex("03"                      // network type (TORv2)
                       "0a"                      // address length
                       "f1f2f3f4f5f6f7f8f9fa")}; // address
    s >> addr;
    BOOST_CHECK(!addr.IsValid());
    BOOST_REQUIRE(s.empty());

    // Valid TORv3.
    s << Span{ParseHex("04"                               // network type (TORv3)
                       "20"                               // address length
                       "79bcc625184b05194975c28b66b66b04" // address
                       "69f7f6556fb1ac3189a79b40dda32f1f"
                       )};
    s >> addr;
    BOOST_CHECK(addr.IsValid());
    BOOST_CHECK(addr.IsTor());
    BOOST_CHECK(!addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(),
                      "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion");
    BOOST_REQUIRE(s.empty());

    // Invalid TORv3, with bogus length.
    s << Span{ParseHex("04" // network type (TORv3)
                       "00" // address length
                       "00" // address
                       )};
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure,
                          HasReason("BIP155 TORv3 address with length 0 (should be 32)"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Valid I2P.
    s << Span{ParseHex("05"                               // network type (I2P)
                       "20"                               // address length
                       "a2894dabaec08c0051a481a6dac88b64" // address
                       "f98232ae42d4b6fd2fa81952dfe36a87")};
    s >> addr;
    BOOST_CHECK(addr.IsValid());
    BOOST_CHECK(addr.IsI2P());
    BOOST_CHECK(!addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(),
                      "ukeu3k5oycgaauneqgtnvselmt4yemvoilkln7jpvamvfx7dnkdq.b32.i2p");
    BOOST_REQUIRE(s.empty());

    // Invalid I2P, with bogus length.
    s << Span{ParseHex("05" // network type (I2P)
                       "03" // address length
                       "00" // address
                       )};
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure,
                          HasReason("BIP155 I2P address with length 3 (should be 32)"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Valid CJDNS.
    s << Span{ParseHex("06"                               // network type (CJDNS)
                       "10"                               // address length
                       "fc000001000200030004000500060007" // address
                       )};
    s >> addr;
    BOOST_CHECK(addr.IsValid());
    BOOST_CHECK(addr.IsCJDNS());
    BOOST_CHECK(!addr.IsAddrV1Compatible());
    BOOST_CHECK_EQUAL(addr.ToStringAddr(), "fc00:1:2:3:4:5:6:7");
    BOOST_REQUIRE(s.empty());

    // Invalid CJDNS, wrong prefix.
    s << Span{ParseHex("06"                               // network type (CJDNS)
                       "10"                               // address length
                       "aa000001000200030004000500060007" // address
                       )};
    s >> addr;
    BOOST_CHECK(addr.IsCJDNS());
    BOOST_CHECK(!addr.IsValid());
    BOOST_REQUIRE(s.empty());

    // Invalid CJDNS, with bogus length.
    s << Span{ParseHex("06" // network type (CJDNS)
                       "01" // address length
                       "00" // address
                       )};
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure,
                          HasReason("BIP155 CJDNS address with length 1 (should be 16)"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Unknown, with extreme length.
    s << Span{ParseHex("aa"             // network type (unknown)
                       "fe00000002"     // address length (CompactSize's MAX_SIZE)
                       "01020304050607" // address
                       )};
    BOOST_CHECK_EXCEPTION(s >> addr, std::ios_base::failure,
                          HasReason("Address too long: 33554432 > 512"));
    BOOST_REQUIRE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Unknown, with reasonable length.
    s << Span{ParseHex("aa"       // network type (unknown)
                       "04"       // address length
                       "01020304" // address
                       )};
    s >> addr;
    BOOST_CHECK(!addr.IsValid());
    BOOST_REQUIRE(s.empty());

    // Unknown, with zero length.
    s << Span{ParseHex("aa" // network type (unknown)
                       "00" // address length
                       ""   // address
                       )};
    s >> addr;
    BOOST_CHECK(!addr.IsValid());
    BOOST_REQUIRE(s.empty());
}

// prior to PR #14728, this test triggers an undefined behavior
BOOST_AUTO_TEST_CASE(ipv4_peer_with_ipv6_addrMe_test)
{
    // set up local addresses; all that's necessary to reproduce the bug is
    // that a normal IPv4 address is among the entries, but if this address is
    // !IsRoutable the undefined behavior is easier to trigger deterministically
    in_addr raw_addr;
    raw_addr.s_addr = htonl(0x7f000001);
    const CNetAddr mapLocalHost_entry = CNetAddr(raw_addr);
    {
        LOCK(g_maplocalhost_mutex);
        LocalServiceInfo lsi;
        lsi.nScore = 23;
        lsi.nPort = 42;
        mapLocalHost[mapLocalHost_entry] = lsi;
    }

    // create a peer with an IPv4 address
    in_addr ipv4AddrPeer;
    ipv4AddrPeer.s_addr = 0xa0b0c001;
    CAddress addr = CAddress(CService(ipv4AddrPeer, 7777), NODE_NETWORK);
    std::unique_ptr<CNode> pnode = std::make_unique<CNode>(/*id=*/0,
                                                           /*sock=*/nullptr,
                                                           addr,
                                                           /*nKeyedNetGroupIn=*/0,
                                                           /*nLocalHostNonceIn=*/0,
                                                           CAddress{},
                                                           /*pszDest=*/std::string{},
                                                           ConnectionType::OUTBOUND_FULL_RELAY,
                                                           /*inbound_onion=*/false);
    pnode->fSuccessfullyConnected.store(true);

    // the peer claims to be reaching us via IPv6
    in6_addr ipv6AddrLocal;
    memset(ipv6AddrLocal.s6_addr, 0, 16);
    ipv6AddrLocal.s6_addr[0] = 0xcc;
    CAddress addrLocal = CAddress(CService(ipv6AddrLocal, 7777), NODE_NETWORK);
    pnode->SetAddrLocal(addrLocal);

    // before patch, this causes undefined behavior detectable with clang's -fsanitize=memory
    GetLocalAddrForPeer(*pnode);

    // suppress no-checks-run warning; if this test fails, it's by triggering a sanitizer
    BOOST_CHECK(1);

    // Cleanup, so that we don't confuse other tests.
    {
        LOCK(g_maplocalhost_mutex);
        mapLocalHost.erase(mapLocalHost_entry);
    }
}

BOOST_AUTO_TEST_CASE(get_local_addr_for_peer_port)
{
    // Test that GetLocalAddrForPeer() properly selects the address to self-advertise:
    //
    // 1. GetLocalAddrForPeer() calls GetLocalAddress() which returns an address that is
    //    not routable.
    // 2. GetLocalAddrForPeer() overrides the address with whatever the peer has told us
    //    he sees us as.
    // 2.1. For inbound connections we must override both the address and the port.
    // 2.2. For outbound connections we must override only the address.

    // Pretend that we bound to this port.
    const uint16_t bind_port = 20001;
    m_node.args->ForceSetArg("-bind", strprintf("3.4.5.6:%u", bind_port));

    // Our address:port as seen from the peer, completely different from the above.
    in_addr peer_us_addr;
    peer_us_addr.s_addr = htonl(0x02030405);
    const CService peer_us{peer_us_addr, 20002};

    // Create a peer with a routable IPv4 address (outbound).
    in_addr peer_out_in_addr;
    peer_out_in_addr.s_addr = htonl(0x01020304);
    CNode peer_out{/*id=*/0,
                   /*sock=*/nullptr,
                   /*addrIn=*/CAddress{CService{peer_out_in_addr, 8333}, NODE_NETWORK},
                   /*nKeyedNetGroupIn=*/0,
                   /*nLocalHostNonceIn=*/0,
                   /*addrBindIn=*/CAddress{},
                   /*addrNameIn=*/std::string{},
                   /*conn_type_in=*/ConnectionType::OUTBOUND_FULL_RELAY,
                   /*inbound_onion=*/false};
    peer_out.fSuccessfullyConnected = true;
    peer_out.SetAddrLocal(peer_us);

    // Without the fix peer_us:8333 is chosen instead of the proper peer_us:bind_port.
    auto chosen_local_addr = GetLocalAddrForPeer(peer_out);
    BOOST_REQUIRE(chosen_local_addr);
    const CService expected{peer_us_addr, bind_port};
    BOOST_CHECK(*chosen_local_addr == expected);

    // Create a peer with a routable IPv4 address (inbound).
    in_addr peer_in_in_addr;
    peer_in_in_addr.s_addr = htonl(0x05060708);
    CNode peer_in{/*id=*/0,
                  /*sock=*/nullptr,
                  /*addrIn=*/CAddress{CService{peer_in_in_addr, 8333}, NODE_NETWORK},
                  /*nKeyedNetGroupIn=*/0,
                  /*nLocalHostNonceIn=*/0,
                  /*addrBindIn=*/CAddress{},
                  /*addrNameIn=*/std::string{},
                  /*conn_type_in=*/ConnectionType::INBOUND,
                  /*inbound_onion=*/false};
    peer_in.fSuccessfullyConnected = true;
    peer_in.SetAddrLocal(peer_us);

    // Without the fix peer_us:8333 is chosen instead of the proper peer_us:peer_us.GetPort().
    chosen_local_addr = GetLocalAddrForPeer(peer_in);
    BOOST_REQUIRE(chosen_local_addr);
    BOOST_CHECK(*chosen_local_addr == peer_us);

    m_node.args->ForceSetArg("-bind", "");
}

BOOST_AUTO_TEST_CASE(LimitedAndReachable_Network)
{
    BOOST_CHECK(IsReachable(NET_IPV4));
    BOOST_CHECK(IsReachable(NET_IPV6));
    BOOST_CHECK(IsReachable(NET_ONION));
    BOOST_CHECK(IsReachable(NET_I2P));
    BOOST_CHECK(IsReachable(NET_CJDNS));

    SetReachable(NET_IPV4, false);
    SetReachable(NET_IPV6, false);
    SetReachable(NET_ONION, false);
    SetReachable(NET_I2P, false);
    SetReachable(NET_CJDNS, false);

    BOOST_CHECK(!IsReachable(NET_IPV4));
    BOOST_CHECK(!IsReachable(NET_IPV6));
    BOOST_CHECK(!IsReachable(NET_ONION));
    BOOST_CHECK(!IsReachable(NET_I2P));
    BOOST_CHECK(!IsReachable(NET_CJDNS));

    SetReachable(NET_IPV4, true);
    SetReachable(NET_IPV6, true);
    SetReachable(NET_ONION, true);
    SetReachable(NET_I2P, true);
    SetReachable(NET_CJDNS, true);

    BOOST_CHECK(IsReachable(NET_IPV4));
    BOOST_CHECK(IsReachable(NET_IPV6));
    BOOST_CHECK(IsReachable(NET_ONION));
    BOOST_CHECK(IsReachable(NET_I2P));
    BOOST_CHECK(IsReachable(NET_CJDNS));
}

BOOST_AUTO_TEST_CASE(LimitedAndReachable_NetworkCaseUnroutableAndInternal)
{
    BOOST_CHECK(IsReachable(NET_UNROUTABLE));
    BOOST_CHECK(IsReachable(NET_INTERNAL));

    SetReachable(NET_UNROUTABLE, false);
    SetReachable(NET_INTERNAL, false);

    BOOST_CHECK(IsReachable(NET_UNROUTABLE)); // Ignored for both networks
    BOOST_CHECK(IsReachable(NET_INTERNAL));
}

CNetAddr UtilBuildAddress(unsigned char p1, unsigned char p2, unsigned char p3, unsigned char p4)
{
    unsigned char ip[] = {p1, p2, p3, p4};

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sockaddr_in)); // initialize the memory block
    memcpy(&(sa.sin_addr), &ip, sizeof(ip));
    return CNetAddr(sa.sin_addr);
}


BOOST_AUTO_TEST_CASE(LimitedAndReachable_CNetAddr)
{
    CNetAddr addr = UtilBuildAddress(0x001, 0x001, 0x001, 0x001); // 1.1.1.1

    SetReachable(NET_IPV4, true);
    BOOST_CHECK(IsReachable(addr));

    SetReachable(NET_IPV4, false);
    BOOST_CHECK(!IsReachable(addr));

    SetReachable(NET_IPV4, true); // have to reset this, because this is stateful.
}


BOOST_AUTO_TEST_CASE(LocalAddress_BasicLifecycle)
{
    CService addr = CService(UtilBuildAddress(0x002, 0x001, 0x001, 0x001), 1000); // 2.1.1.1:1000

    SetReachable(NET_IPV4, true);

    BOOST_CHECK(!IsLocal(addr));
    BOOST_CHECK(AddLocal(addr, 1000));
    BOOST_CHECK(IsLocal(addr));

    RemoveLocal(addr);
    BOOST_CHECK(!IsLocal(addr));
}

BOOST_AUTO_TEST_CASE(initial_advertise_from_version_message)
{
    LOCK(NetEventsInterface::g_msgproc_mutex);

    // Tests the following scenario:
    // * -bind=3.4.5.6:20001 is specified
    // * we make an outbound connection to a peer
    // * the peer reports he sees us as 2.3.4.5:20002 in the version message
    //   (20002 is a random port assigned by our OS for the outgoing TCP connection,
    //   we cannot accept connections to it)
    // * we should self-advertise to that peer as 2.3.4.5:20001

    // Pretend that we bound to this port.
    const uint16_t bind_port = 20001;
    m_node.args->ForceSetArg("-bind", strprintf("3.4.5.6:%u", bind_port));
    m_node.args->ForceSetArg("-capturemessages", "1");

    // Our address:port as seen from the peer - 2.3.4.5:20002 (different from the above).
    in_addr peer_us_addr;
    peer_us_addr.s_addr = htonl(0x02030405);
    const CService peer_us{peer_us_addr, 20002};

    // Create a peer with a routable IPv4 address.
    in_addr peer_in_addr;
    peer_in_addr.s_addr = htonl(0x01020304);
    CNode peer{/*id=*/0,
               /*sock=*/nullptr,
               /*addrIn=*/CAddress{CService{peer_in_addr, 8333}, NODE_NETWORK},
               /*nKeyedNetGroupIn=*/0,
               /*nLocalHostNonceIn=*/0,
               /*addrBindIn=*/CAddress{},
               /*addrNameIn=*/std::string{},
               /*conn_type_in=*/ConnectionType::OUTBOUND_FULL_RELAY,
               /*inbound_onion=*/false};

    const ServiceFlags nServices{NODE_NETWORK | NODE_WITNESS};
    const int64_t time{0};
    const CNetMsgMaker msg_maker{PROTOCOL_VERSION};

    // The test connman starts with a zero send buffer, so any queued handshake
    // byte sets fPauseSend and SendMessages then skips address relay. This
    // case checks the advertisement, so give it the production pause threshold.
    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    const unsigned int prev_send_buffer = connman.TestSendBufferMax();
    connman.SetTestSendBufferMax(1000 * 1000);

    // Force Chainstate::IsInitialBlockDownload() to return false.
    // Otherwise PushAddress() isn't called by PeerManager::ProcessMessage().
    TestChainState& chainstate =
        *static_cast<TestChainState*>(&m_node.chainman->ActiveChainstate());
    chainstate.JumpOutOfIbd();

    m_node.peerman->InitializeNode(peer, NODE_NETWORK);

    std::atomic<bool> interrupt_dummy{false};
    std::chrono::microseconds time_received_dummy{0};

    const auto msg_version = msg_maker.Make(
        NetMsgType::VERSION,
        RIGOR_PROTOCOL_CURRENT_VERSION,
        Using<CustomUintFormatter<8>>(nServices),
        time,
        Using<CustomUintFormatter<8>>(nServices),
        peer_us,
        int64_t{},
        CService{},
        uint64_t{1},
        std::string{},
        int32_t{},
        true);
    CDataStream msg_version_stream{msg_version.data, SER_NETWORK, PROTOCOL_VERSION};

    m_node.peerman->ProcessMessage(
        peer, NetMsgType::VERSION, msg_version_stream, time_received_dummy, interrupt_dummy);

    const auto msg_rigorcap = msg_maker.Make(NetMsgType::RIGORCAP,
                                             static_cast<uint32_t>(RIGOR_PROTOCOL_CURRENT_VERSION),
                                             RIGOR_LOCAL_ACTIVE_CAPABILITIES);
    CDataStream msg_rigorcap_stream{msg_rigorcap.data, SER_NETWORK, PROTOCOL_VERSION};
    m_node.peerman->ProcessMessage(
        peer, NetMsgType::RIGORCAP, msg_rigorcap_stream, time_received_dummy, interrupt_dummy);

    const auto msg_verack = msg_maker.Make(NetMsgType::VERACK);
    CDataStream msg_verack_stream{msg_verack.data, SER_NETWORK, PROTOCOL_VERSION};

    // Will set peer.fSuccessfullyConnected to true (necessary in SendMessages()).
    m_node.peerman->ProcessMessage(
        peer, NetMsgType::VERACK, msg_verack_stream, time_received_dummy, interrupt_dummy);

    // Ensure that peer_us_addr:bind_port is sent to the peer.
    const CService expected{peer_us_addr, bind_port};
    bool sent{false};

    const auto CaptureMessageOrig = CaptureMessage;
    CaptureMessage = [&sent, &expected](const CAddress& addr,
                                        const std::string& msg_type,
                                        Span<const unsigned char> data,
                                        bool is_incoming) -> void {
        if (!is_incoming && msg_type == "addrv2") {
            CDataStream s(data, SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
            std::vector<CAddress> addresses;

            s >> addresses;

            for (const auto& addr : addresses) {
                if (addr == expected) {
                    sent = true;
                    return;
                }
            }
        }
    };

    m_node.peerman->SendMessages(&peer);

    BOOST_CHECK(sent);

    CaptureMessage = CaptureMessageOrig;
    connman.SetTestSendBufferMax(prev_send_buffer);
    chainstate.ResetIbd();
    m_node.args->ForceSetArg("-capturemessages", "0");
    m_node.args->ForceSetArg("-bind", "");
    // PeerManager::ProcessMessage() calls AddTimeData() which changes the internal state
    // in timedata.cpp and later confuses the test "timedata_tests/addtimedata". Thus reset
    // that state as it was before our test was run.
    TestOnlyResetTimeData();
}

BOOST_AUTO_TEST_CASE(inbound_onion_does_not_inherit_localhost_whitelist)
{
    BOOST_CHECK(!WhitelistAppliesToAcceptedInbound(/*inbound_onion=*/true));
    BOOST_CHECK(WhitelistAppliesToAcceptedInbound(/*inbound_onion=*/false));

    bilingual_str error;
    NetWhitelistPermissions localhost;
    BOOST_REQUIRE(NetWhitelistPermissions::TryParse("127.0.0.1", localhost, error));
    CNetAddr tor_daemon;
    BOOST_REQUIRE(LookupHost("127.0.0.1", tor_daemon, false));
    BOOST_CHECK(localhost.m_subnet.Match(tor_daemon));

    const auto apply = [&](bool inbound_onion) {
        NetPermissionFlags flags = NetPermissionFlags::None;
        if (WhitelistAppliesToAcceptedInbound(inbound_onion) && localhost.m_subnet.Match(tor_daemon)) {
            NetPermissions::AddFlag(flags, localhost.m_flags);
        }
        return flags;
    };

    const NetPermissionFlags onion = apply(true);
    const NetPermissionFlags clearnet = apply(false);
    BOOST_CHECK(!NetPermissions::HasFlag(onion, NetPermissionFlags::Implicit));
    BOOST_CHECK(!NetPermissions::HasFlag(onion, NetPermissionFlags::NoBan));
    BOOST_CHECK(NetPermissions::HasFlag(clearnet, NetPermissionFlags::Implicit));

    CNetAddr remote;
    BOOST_REQUIRE(LookupHost("1.2.3.4", remote, false));
    BOOST_CHECK(!localhost.m_subnet.Match(remote));

    NetPermissionFlags explicit_remote = NetPermissionFlags::None;
    NetPermissions::AddFlag(explicit_remote, NetPermissionFlags::NoBan);
    BOOST_CHECK(NetPermissions::HasFlag(explicit_remote, NetPermissionFlags::NoBan));
}

BOOST_AUTO_TEST_SUITE_END()

namespace {

CNetAddr NumericAddr(const std::string& ip)
{
    CNetAddr addr;
    BOOST_REQUIRE_MESSAGE(LookupHost(ip, addr, /*fAllowLookup=*/false), ip);
    return addr;
}

bool ContainsEndpoint(const std::vector<CAddress>& addrs, const CNetAddr& ip, uint16_t port)
{
    for (const CAddress& addr : addrs) {
        if (addr.GetPort() == port && static_cast<const CNetAddr&>(addr) == ip) return true;
    }
    return false;
}

bool SeedsContain(const std::vector<std::string>& seeds, const std::string& needle)
{
    for (const std::string& seed : seeds) {
        if (seed.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::vector<CService> DecodeFixedSeeds(const std::vector<uint8_t>& raw)
{
    std::vector<CService> out;
    CDataStream s(raw, SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    while (!s.eof()) {
        CService endpoint;
        s >> endpoint;
        out.push_back(endpoint);
    }
    return out;
}

void CheckMainnetFixedSeed(const CChainParams& mainnet)
{
    const std::vector<CService> seeds = DecodeFixedSeeds(mainnet.FixedSeeds());
    BOOST_REQUIRE_EQUAL(seeds.size(), 1U);
    BOOST_CHECK(seeds[0].IsIPv4());
    BOOST_CHECK_EQUAL(seeds[0].GetPort(), static_cast<uint16_t>(8760));
    BOOST_CHECK_EQUAL(seeds[0].ToStringAddrPort(), "80.96.108.2:8760");
    for (const CService& endpoint : seeds) {
        BOOST_CHECK(endpoint.ToStringAddrPort() != "167.88.172.182:8760");
    }
}

} // namespace

BOOST_FIXTURE_TEST_SUITE(dns_seed_discovery_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(mainnet_and_testnet_seeds_are_isolated)
{
    const auto mainnet = CChainParams::Main();
    const auto testnet = CChainParams::TestNet();
    const auto devtest = CChainParams::DevTest({});
    const auto regtest = CChainParams::RegTest({});
    const auto slowtest = CChainParams::SlowTest({});

    const std::vector<std::string>& main_seeds = mainnet->DNSSeeds();
    const std::vector<std::string>& test_seeds = testnet->DNSSeeds();

    BOOST_REQUIRE(!main_seeds.empty());
    BOOST_CHECK_EQUAL(main_seeds.size(), 2U);
    BOOST_CHECK(std::find(main_seeds.begin(), main_seeds.end(), "seed.rigorchain.com.") != main_seeds.end());
    BOOST_CHECK(std::find(main_seeds.begin(), main_seeds.end(), "seed1.rigorchain.com.") != main_seeds.end());
    BOOST_CHECK(std::find(main_seeds.begin(), main_seeds.end(), "seed.rigor.network.") == main_seeds.end());
    BOOST_CHECK(!SeedsContain(main_seeds, "rigor.network"));
    BOOST_CHECK(!SeedsContain(main_seeds, "testnet"));
    CheckMainnetFixedSeed(*mainnet);

    BOOST_CHECK(!SeedsContain(test_seeds, "rigorchain.com"));
    BOOST_CHECK(std::find(test_seeds.begin(), test_seeds.end(), "seed.rigor.network.") == test_seeds.end());
    BOOST_CHECK(std::find(test_seeds.begin(), test_seeds.end(), "seed1.testnet.rigor.network.") != test_seeds.end());
    for (const std::string& seed : test_seeds) {
        BOOST_CHECK(std::find(main_seeds.begin(), main_seeds.end(), seed) == main_seeds.end());
    }

    BOOST_CHECK(!SeedsContain(devtest->DNSSeeds(), "rigorchain.com"));
    BOOST_CHECK(!SeedsContain(regtest->DNSSeeds(), "rigorchain.com"));
    BOOST_CHECK(!SeedsContain(slowtest->DNSSeeds(), "rigorchain.com"));

    const std::vector<CService> test_fixed = DecodeFixedSeeds(testnet->FixedSeeds());
    BOOST_REQUIRE_EQUAL(test_fixed.size(), 1U);
    BOOST_CHECK(test_fixed[0].IsLocal());
    BOOST_CHECK_EQUAL(test_fixed[0].ToStringAddrPort(), "127.0.0.1:18760");
    BOOST_CHECK(DecodeFixedSeeds(devtest->FixedSeeds()).empty());
    BOOST_CHECK(DecodeFixedSeeds(regtest->FixedSeeds()).empty());
    BOOST_CHECK(DecodeFixedSeeds(slowtest->FixedSeeds()).empty());

    BOOST_CHECK_EQUAL(mainnet->GetDefaultPort(), 8760);
    BOOST_CHECK_EQUAL(testnet->GetDefaultPort(), 18760);
    BOOST_CHECK_EQUAL(CreateBaseChainParams(CBaseChainParams::MAIN)->RPCPort(), 8761);
    BOOST_CHECK_EQUAL(CreateBaseChainParams(CBaseChainParams::TESTNET)->RPCPort(), 18761);
    bool magic_differs = false;
    for (size_t i = 0; i < CMessageHeader::MESSAGE_START_SIZE; ++i) {
        if (mainnet->MessageStart()[i] != testnet->MessageStart()[i]) magic_differs = true;
    }
    BOOST_CHECK(magic_differs);

    // Discovery edit must not move mainnet identity.
    BOOST_CHECK_EQUAL(
        mainnet->GetConsensus().hashGenesisBlock,
        uint256S("0xe2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468"));
    BOOST_CHECK_EQUAL(mainnet->Bech32HRP(), "rg");
    BOOST_CHECK_EQUAL(testnet->Bech32HRP(), "trg");
}

BOOST_AUTO_TEST_CASE(dns_seed_imports_bare_a_records_on_chain_port)
{
    const ServiceFlags services{NODE_NETWORK | NODE_WITNESS};
    const CNetAddr long_term = NumericAddr("198.51.100.10");
    const CNetAddr temporary = NumericAddr("198.51.100.20");
    const CNetAddr future = NumericAddr("203.0.113.10");
    // Globally valid IPv6 (not 2001:db8, which CNetAddr::IsValid rejects as RFC3849).
    const CNetAddr future_v6 = NumericAddr("1122:3344:5566:7788:9900:aabb:ccdd:eeff");

    // Test 1 — seed answers with current nodes. Wallet learns both on the P2P port.
    {
        const DnsSeedImport imported = ResolveDnsSeedImport({}, {long_term, temporary}, 8760, services);
        BOOST_CHECK(imported.kind == DnsSeedLookupKind::BARE_NAME);
        BOOST_CHECK_EQUAL(imported.addresses.size(), 2U);
        BOOST_CHECK(ContainsEndpoint(imported.addresses, long_term, 8760));
        BOOST_CHECK(ContainsEndpoint(imported.addresses, temporary, 8760));
        BOOST_CHECK(imported.addresses[0].nServices == services);
    }

    // Test 2 — DNS returns nothing. No addresses, no throw; caller uses ADDR_FETCH.
    {
        const DnsSeedImport imported = ResolveDnsSeedImport({}, {}, 8760, services);
        BOOST_CHECK(imported.kind == DnsSeedLookupKind::ADDR_FETCH);
        BOOST_CHECK(imported.addresses.empty());
    }

    // Test 3 — temporary node absent. The long-term node is still discovered.
    {
        const DnsSeedImport imported = ResolveDnsSeedImport({}, {long_term}, 8760, services);
        BOOST_CHECK(ContainsEndpoint(imported.addresses, long_term, 8760));
        BOOST_CHECK(!ContainsEndpoint(imported.addresses, temporary, 8760));
    }

    // Test 4 — long-term node absent. Another DNS answer is still usable.
    {
        const DnsSeedImport imported = ResolveDnsSeedImport({}, {future}, 8760, services);
        BOOST_CHECK(ContainsEndpoint(imported.addresses, future, 8760));
        BOOST_CHECK(!ContainsEndpoint(imported.addresses, long_term, 8760));
        BOOST_CHECK(!ContainsEndpoint(imported.addresses, temporary, 8760));
    }

    // Test 5 — a new server appears only in DNS. The compiled seed list is unchanged.
    {
        const DnsSeedImport imported = ResolveDnsSeedImport({}, {long_term, future, future_v6}, 8760, services);
        BOOST_CHECK(ContainsEndpoint(imported.addresses, long_term, 8760));
        BOOST_CHECK(ContainsEndpoint(imported.addresses, future, 8760));
        BOOST_CHECK(ContainsEndpoint(imported.addresses, future_v6, 8760));
        const auto mainnet = CChainParams::Main();
        BOOST_CHECK(!SeedsContain(mainnet->DNSSeeds(), "203.0.113.10"));
        BOOST_CHECK(!SeedsContain(mainnet->DNSSeeds(), "1122:3344"));
        CheckMainnetFixedSeed(*mainnet);
    }

    // Test 6 — the same answers on testnet use the testnet port, not mainnet.
    {
        const DnsSeedImport main_import = ResolveDnsSeedImport({}, {long_term}, 8760, services);
        const DnsSeedImport test_import = ResolveDnsSeedImport({}, {long_term}, 18760, services);
        BOOST_CHECK(ContainsEndpoint(main_import.addresses, long_term, 8760));
        BOOST_CHECK(!ContainsEndpoint(main_import.addresses, long_term, 18760));
        BOOST_CHECK(ContainsEndpoint(test_import.addresses, long_term, 18760));
        BOOST_CHECK(!ContainsEndpoint(test_import.addresses, long_term, 8761));
        BOOST_CHECK(!ContainsEndpoint(test_import.addresses, long_term, 18761));
    }

    // A filtering seeder still wins over the bare name.
    {
        const DnsSeedImport imported = ResolveDnsSeedImport({future}, {long_term, temporary}, 8760, services);
        BOOST_CHECK(imported.kind == DnsSeedLookupKind::SERVICE_FILTERED);
        BOOST_CHECK(ContainsEndpoint(imported.addresses, future, 8760));
        BOOST_CHECK(!ContainsEndpoint(imported.addresses, long_term, 8760));
        BOOST_CHECK(!ContainsEndpoint(imported.addresses, temporary, 8760));
    }
}

BOOST_AUTO_TEST_CASE(automatic_port_mapping_disabled)
{
    BOOST_CHECK(!AutomaticPortMappingAvailable());
    StartMapPort(true, true);
    BOOST_CHECK(!MapPortThreadActive());
    InterruptMapPort();
    StopMapPort();
    StartMapPort(false, false);
    BOOST_CHECK(!MapPortThreadActive());

    const uint16_t listen_port = GetListenPort();
    StartMapPort(true, true);
    BOOST_CHECK_EQUAL(GetListenPort(), listen_port);
    BOOST_CHECK(listen_port != CreateBaseChainParams(CBaseChainParams::MAIN)->RPCPort());
    BOOST_CHECK(listen_port != CreateBaseChainParams(CBaseChainParams::MAIN)->OnionServiceTargetPort());
    BOOST_CHECK_EQUAL(CChainParams::Main()->GetDefaultPort(), 8760);
    BOOST_CHECK_EQUAL(CreateBaseChainParams(CBaseChainParams::MAIN)->RPCPort(), 8761);
    BOOST_CHECK_EQUAL(CreateBaseChainParams(CBaseChainParams::MAIN)->OnionServiceTargetPort(), 8762);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(net_tests_chain100, TestChain100Setup)

BOOST_AUTO_TEST_CASE(paused_getblocks_response_stays_one_batch)
{
    auto& connman = static_cast<ConnmanTestMsg&>(*m_node.connman);
    auto& peerman = *m_node.peerman;
    connman.SetTestSendBufferMax(1000 * 1000);
    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;
    const CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    CNode paused{/*id=*/9401,
                 /*sock=*/nullptr,
                 addr,
                 /*nKeyedNetGroupIn=*/0,
                 /*nLocalHostNonceIn=*/0,
                 CAddress{},
                 /*addrNameIn=*/std::string{},
                 ConnectionType::OUTBOUND_FULL_RELAY,
                 /*inbound_onion=*/false};
    CNode healthy{/*id=*/9402,
                  /*sock=*/nullptr,
                  addr,
                  /*nKeyedNetGroupIn=*/1,
                  /*nLocalHostNonceIn=*/1,
                  CAddress{},
                  /*addrNameIn=*/std::string{},
                  ConnectionType::OUTBOUND_FULL_RELAY,
                  /*inbound_onion=*/false};
    struct Cleanup {
        ConnmanTestMsg& connman;
        PeerManager& peerman;
        CNode& paused;
        CNode& healthy;
        bool finalized{false};
        ~Cleanup()
        {
            connman.RemoveTestNode(paused);
            connman.RemoveTestNode(healthy);
            if (!finalized) {
                peerman.FinalizeNode(paused);
                peerman.FinalizeNode(healthy);
            }
        }
    } cleanup{connman, peerman, paused, healthy};

    const ServiceFlags services{NODE_NETWORK | NODE_WITNESS};
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        connman.Handshake(paused, true, services, services, RIGOR_PROTOCOL_CURRENT_VERSION, true);
        connman.Handshake(healthy, true, services, services, RIGOR_PROTOCOL_CURRENT_VERSION, true);
    }
    BOOST_REQUIRE(paused.fSuccessfullyConnected);
    BOOST_REQUIRE(healthy.fSuccessfullyConnected);
    BOOST_REQUIRE(!paused.fDisconnect);
    BOOST_REQUIRE(!healthy.fDisconnect);
    connman.AddTestNode(paused);
    connman.AddTestNode(healthy);

    const CNetMsgMaker msg_maker{PROTOCOL_VERSION};
    std::atomic<bool> interrupt{false};
    const auto time_received = std::chrono::microseconds{0};
    const int height = WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Height());
    BOOST_REQUIRE(height > 8);
    const uint256 genesis = WITH_LOCK(cs_main, return m_node.chainman->ActiveChain()[0]->GetBlockHash());
    auto request_blocks = [&](CNode& node) {
        CBlockLocator locator{std::vector<uint256>{genesis}};
        const auto msg = msg_maker.Make(NetMsgType::GETBLOCKS, locator, uint256{});
        CDataStream stream{msg.data, SER_NETWORK, PROTOCOL_VERSION};
        LOCK(NetEventsInterface::g_msgproc_mutex);
        peerman.ProcessMessage(node, NetMsgType::GETBLOCKS, stream, time_received, interrupt);
    };

    paused.SetSendPaused(true);
    request_blocks(paused);
    const size_t one = peerman.GetGetBlocksResponseCount(paused.GetId());
    BOOST_CHECK_EQUAL(one, static_cast<size_t>(height));
    BOOST_CHECK(one > 8U);
    BOOST_CHECK(one <= 500U);
    for (int i = 0; i < 10; ++i) {
        request_blocks(paused);
    }
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(paused.GetId()), one);
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(paused.GetId()), 0U);

    const CBlockIndex* tip = WITH_LOCK(cs_main, return m_node.chainman->ActiveChain().Tip());
    for (int i = 0; i < 20; ++i) {
        peerman.TestAnnounceBlockTip(tip);
    }
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(paused.GetId()), 8U);
    BOOST_CHECK(peerman.GetNewestBlockAnnouncement(paused.GetId()) == tip->GetBlockHash());
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(paused.GetId()), one);

    const auto paused_bytes = WITH_LOCK(paused.cs_vSend, return paused.nSendSize);
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        BOOST_CHECK(peerman.SendMessages(&paused));
    }
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(paused.GetId()), one);
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(paused.GetId()), 8U);
    BOOST_CHECK(WITH_LOCK(paused.cs_vSend, return paused.nSendSize) - paused_bytes < 1000);

    healthy.SetSendPaused(false);
    request_blocks(healthy);
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(healthy.GetId()), one);
    request_blocks(healthy);
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(healthy.GetId()), one * 2);
    const auto healthy_bytes = WITH_LOCK(healthy.cs_vSend, return healthy.nSendSize);
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        BOOST_CHECK(peerman.SendMessages(&healthy));
    }
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(healthy.GetId()), 0U);
    BOOST_CHECK(WITH_LOCK(healthy.cs_vSend, return healthy.nSendSize) > healthy_bytes + 1000);

    paused.SetSendPaused(false);
    const auto resume_bytes = WITH_LOCK(paused.cs_vSend, return paused.nSendSize);
    {
        LOCK(NetEventsInterface::g_msgproc_mutex);
        BOOST_CHECK(peerman.SendMessages(&paused));
    }
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(paused.GetId()), 0U);
    BOOST_CHECK(WITH_LOCK(paused.cs_vSend, return paused.nSendSize) > resume_bytes + 1000);

    peerman.FinalizeNode(paused);
    peerman.FinalizeNode(healthy);
    cleanup.finalized = true;
    BOOST_CHECK_EQUAL(peerman.GetGetBlocksResponseCount(paused.GetId()), 0U);
    BOOST_CHECK_EQUAL(peerman.GetBlockAnnouncementCount(paused.GetId()), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
