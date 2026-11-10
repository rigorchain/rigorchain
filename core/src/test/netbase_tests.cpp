// Copyright (c) 2012-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net_permissions.h>
#include <netaddress.h>
#include <netbase.h>
#include <netgroup.h>
#include <protocol.h>
#include <serialize.h>
#include <streams.h>
#include <test/util/net.h>
#include <test/util/setup_common.h>
#include <util/sock.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <version.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <string>

#include <boost/test/unit_test.hpp>

using namespace std::literals;

BOOST_FIXTURE_TEST_SUITE(netbase_tests, BasicTestingSetup)

static CNetAddr ResolveIP(const std::string& ip)
{
    CNetAddr addr;
    LookupHost(ip, addr, false);
    return addr;
}

static CSubNet ResolveSubNet(const std::string& subnet)
{
    CSubNet ret;
    LookupSubNet(subnet, ret);
    return ret;
}

static CNetAddr CreateInternal(const std::string& host)
{
    CNetAddr addr;
    addr.SetInternal(host);
    return addr;
}

BOOST_AUTO_TEST_CASE(netbase_networks)
{
    BOOST_CHECK(ResolveIP("127.0.0.1").GetNetwork() == NET_UNROUTABLE);
    BOOST_CHECK(ResolveIP("::1").GetNetwork() == NET_UNROUTABLE);
    BOOST_CHECK(ResolveIP("8.8.8.8").GetNetwork() == NET_IPV4);
    BOOST_CHECK(ResolveIP("2001::8888").GetNetwork() == NET_IPV6);
    BOOST_CHECK(ResolveIP("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion").GetNetwork() == NET_ONION);
    BOOST_CHECK(CreateInternal("foo.com").GetNetwork() == NET_INTERNAL);
}

BOOST_AUTO_TEST_CASE(netbase_properties)
{

    BOOST_CHECK(ResolveIP("127.0.0.1").IsIPv4());
    BOOST_CHECK(ResolveIP("::FFFF:192.168.1.1").IsIPv4());
    BOOST_CHECK(ResolveIP("::1").IsIPv6());
    BOOST_CHECK(ResolveIP("10.0.0.1").IsRFC1918());
    BOOST_CHECK(ResolveIP("192.168.1.1").IsRFC1918());
    BOOST_CHECK(ResolveIP("172.31.255.255").IsRFC1918());
    BOOST_CHECK(ResolveIP("198.18.0.0").IsRFC2544());
    BOOST_CHECK(ResolveIP("198.19.255.255").IsRFC2544());
    BOOST_CHECK(ResolveIP("2001:0DB8::").IsRFC3849());
    BOOST_CHECK(ResolveIP("3FFF::").IsRFC9637());
    BOOST_CHECK(ResolveIP("3FFF:0FFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF").IsRFC9637());
    BOOST_CHECK(!ResolveIP("3FFF:1000::").IsRFC9637());
    BOOST_CHECK(ResolveIP("169.254.1.1").IsRFC3927());
    BOOST_CHECK(ResolveIP("2002::1").IsRFC3964());
    BOOST_CHECK(ResolveIP("FC00::").IsRFC4193());
    BOOST_CHECK(ResolveIP("2001::2").IsRFC4380());
    BOOST_CHECK(ResolveIP("2001:10::").IsRFC4843());
    BOOST_CHECK(ResolveIP("2001:20::").IsRFC7343());
    BOOST_CHECK(ResolveIP("FE80::").IsRFC4862());
    BOOST_CHECK(ResolveIP("64:FF9B::").IsRFC6052());
    BOOST_CHECK(ResolveIP("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion").IsTor());
    BOOST_CHECK(ResolveIP("127.0.0.1").IsLocal());
    BOOST_CHECK(ResolveIP("::1").IsLocal());
    BOOST_CHECK(ResolveIP("8.8.8.8").IsRoutable());
    BOOST_CHECK(ResolveIP("2001::1").IsRoutable());
    BOOST_CHECK(ResolveIP("127.0.0.1").IsValid());
    BOOST_CHECK(!ResolveIP("3FFF::").IsValid());
    BOOST_CHECK(CreateInternal("FD6B:88C0:8724:edb1:8e4:3588:e546:35ca").IsInternal());
    BOOST_CHECK(CreateInternal("bar.com").IsInternal());

}

bool static TestSplitHost(const std::string& test, const std::string& host, uint16_t port, bool validPort=true)
{
    std::string hostOut;
    uint16_t portOut{0};
    bool validPortOut = SplitHostPort(test, portOut, hostOut);
    return hostOut == host && portOut == port && validPortOut == validPort;
}

BOOST_AUTO_TEST_CASE(netbase_splithost)
{
    BOOST_CHECK(TestSplitHost("www.bitcoincore.org", "www.bitcoincore.org", 0));
    BOOST_CHECK(TestSplitHost("[www.bitcoincore.org]", "www.bitcoincore.org", 0));
    BOOST_CHECK(TestSplitHost("www.bitcoincore.org:80", "www.bitcoincore.org", 80));
    BOOST_CHECK(TestSplitHost("[www.bitcoincore.org]:80", "www.bitcoincore.org", 80));
    BOOST_CHECK(TestSplitHost("127.0.0.1", "127.0.0.1", 0));
    BOOST_CHECK(TestSplitHost("127.0.0.1:8333", "127.0.0.1", 8333));
    BOOST_CHECK(TestSplitHost("[127.0.0.1]", "127.0.0.1", 0));
    BOOST_CHECK(TestSplitHost("[127.0.0.1]:8333", "127.0.0.1", 8333));
    BOOST_CHECK(TestSplitHost("::ffff:127.0.0.1", "::ffff:127.0.0.1", 0));
    BOOST_CHECK(TestSplitHost("[::ffff:127.0.0.1]:8333", "::ffff:127.0.0.1", 8333));
    BOOST_CHECK(TestSplitHost("[::]:8333", "::", 8333));
    BOOST_CHECK(TestSplitHost("::8333", "::8333", 0));
    BOOST_CHECK(TestSplitHost(":8333", "", 8333));
    BOOST_CHECK(TestSplitHost("[]:8333", "", 8333));
    BOOST_CHECK(TestSplitHost("", "", 0));
    BOOST_CHECK(TestSplitHost(":65535", "", 65535));
    BOOST_CHECK(TestSplitHost(":65536", ":65536", 0, false));
    BOOST_CHECK(TestSplitHost(":-1", ":-1", 0, false));
    BOOST_CHECK(TestSplitHost("[]:70001", "[]:70001", 0, false));
    BOOST_CHECK(TestSplitHost("[]:-1", "[]:-1", 0, false));
    BOOST_CHECK(TestSplitHost("[]:-0", "[]:-0", 0, false));
    BOOST_CHECK(TestSplitHost("[]:0", "", 0, false));
    BOOST_CHECK(TestSplitHost("[]:1/2", "[]:1/2", 0, false));
    BOOST_CHECK(TestSplitHost("[]:1E2", "[]:1E2", 0, false));
    BOOST_CHECK(TestSplitHost("127.0.0.1:65536", "127.0.0.1:65536", 0, false));
    BOOST_CHECK(TestSplitHost("127.0.0.1:0", "127.0.0.1", 0, false));
    BOOST_CHECK(TestSplitHost("127.0.0.1:", "127.0.0.1:", 0, false));
    BOOST_CHECK(TestSplitHost("127.0.0.1:1/2", "127.0.0.1:1/2", 0, false));
    BOOST_CHECK(TestSplitHost("127.0.0.1:1E2", "127.0.0.1:1E2", 0, false));
    BOOST_CHECK(TestSplitHost("www.bitcoincore.org:65536", "www.bitcoincore.org:65536", 0, false));
    BOOST_CHECK(TestSplitHost("www.bitcoincore.org:0", "www.bitcoincore.org", 0, false));
    BOOST_CHECK(TestSplitHost("www.bitcoincore.org:", "www.bitcoincore.org:", 0, false));
}

bool static TestParse(std::string src, std::string canon)
{
    CService addr(LookupNumeric(src, 65535));
    return canon == addr.ToStringAddrPort();
}

BOOST_AUTO_TEST_CASE(netbase_lookupnumeric)
{
    BOOST_CHECK(TestParse("127.0.0.1", "127.0.0.1:65535"));
    BOOST_CHECK(TestParse("127.0.0.1:8333", "127.0.0.1:8333"));
    BOOST_CHECK(TestParse("::ffff:127.0.0.1", "127.0.0.1:65535"));
    BOOST_CHECK(TestParse("::", "[::]:65535"));
    BOOST_CHECK(TestParse("[::]:8333", "[::]:8333"));
    BOOST_CHECK(TestParse("[127.0.0.1]", "127.0.0.1:65535"));
    BOOST_CHECK(TestParse(":::", "[::]:0"));

    // verify that an internal address fails to resolve
    BOOST_CHECK(TestParse("[fd6b:88c0:8724:1:2:3:4:5]", "[::]:0"));
    // and that a one-off resolves correctly
    BOOST_CHECK(TestParse("[fd6c:88c0:8724:1:2:3:4:5]", "[fd6c:88c0:8724:1:2:3:4:5]:65535"));
}

BOOST_AUTO_TEST_CASE(embedded_test)
{
    CNetAddr addr1(ResolveIP("1.2.3.4"));
    CNetAddr addr2(ResolveIP("::FFFF:0102:0304"));
    BOOST_CHECK(addr2.IsIPv4());
    BOOST_CHECK_EQUAL(addr1.ToStringAddr(), addr2.ToStringAddr());
}

BOOST_AUTO_TEST_CASE(subnet_test)
{

    BOOST_CHECK(ResolveSubNet("1.2.3.0/24") == ResolveSubNet("1.2.3.0/255.255.255.0"));
    BOOST_CHECK(ResolveSubNet("1.2.3.0/24") != ResolveSubNet("1.2.4.0/255.255.255.0"));
    BOOST_CHECK(ResolveSubNet("1.2.3.0/24").Match(ResolveIP("1.2.3.4")));
    BOOST_CHECK(!ResolveSubNet("1.2.2.0/24").Match(ResolveIP("1.2.3.4")));
    BOOST_CHECK(ResolveSubNet("1.2.3.4").Match(ResolveIP("1.2.3.4")));
    BOOST_CHECK(ResolveSubNet("1.2.3.4/32").Match(ResolveIP("1.2.3.4")));
    BOOST_CHECK(!ResolveSubNet("1.2.3.4").Match(ResolveIP("5.6.7.8")));
    BOOST_CHECK(!ResolveSubNet("1.2.3.4/32").Match(ResolveIP("5.6.7.8")));
    BOOST_CHECK(ResolveSubNet("::ffff:127.0.0.1").Match(ResolveIP("127.0.0.1")));
    BOOST_CHECK(ResolveSubNet("1:2:3:4:5:6:7:8").Match(ResolveIP("1:2:3:4:5:6:7:8")));
    BOOST_CHECK(!ResolveSubNet("1:2:3:4:5:6:7:8").Match(ResolveIP("1:2:3:4:5:6:7:9")));
    BOOST_CHECK(ResolveSubNet("1:2:3:4:5:6:7:0/112").Match(ResolveIP("1:2:3:4:5:6:7:1234")));
    BOOST_CHECK(ResolveSubNet("192.168.0.1/24").Match(ResolveIP("192.168.0.2")));
    BOOST_CHECK(ResolveSubNet("192.168.0.20/29").Match(ResolveIP("192.168.0.18")));
    BOOST_CHECK(ResolveSubNet("1.2.2.1/24").Match(ResolveIP("1.2.2.4")));
    BOOST_CHECK(ResolveSubNet("1.2.2.110/31").Match(ResolveIP("1.2.2.111")));
    BOOST_CHECK(ResolveSubNet("1.2.2.20/26").Match(ResolveIP("1.2.2.63")));
    // All-Matching IPv6 Matches arbitrary IPv6
    BOOST_CHECK(ResolveSubNet("::/0").Match(ResolveIP("1:2:3:4:5:6:7:1234")));
    // But not `::` or `0.0.0.0` because they are considered invalid addresses
    BOOST_CHECK(!ResolveSubNet("::/0").Match(ResolveIP("::")));
    BOOST_CHECK(!ResolveSubNet("::/0").Match(ResolveIP("0.0.0.0")));
    // Addresses from one network (IPv4) don't belong to subnets of another network (IPv6)
    BOOST_CHECK(!ResolveSubNet("::/0").Match(ResolveIP("1.2.3.4")));
    // All-Matching IPv4 does not Match IPv6
    BOOST_CHECK(!ResolveSubNet("0.0.0.0/0").Match(ResolveIP("1:2:3:4:5:6:7:1234")));
    // Invalid subnets Match nothing (not even invalid addresses)
    BOOST_CHECK(!CSubNet().Match(ResolveIP("1.2.3.4")));
    BOOST_CHECK(!ResolveSubNet("").Match(ResolveIP("4.5.6.7")));
    BOOST_CHECK(!ResolveSubNet("bloop").Match(ResolveIP("0.0.0.0")));
    BOOST_CHECK(!ResolveSubNet("bloop").Match(ResolveIP("hab")));
    // Check valid/invalid
    BOOST_CHECK(ResolveSubNet("1.2.3.0/0").IsValid());
    BOOST_CHECK(!ResolveSubNet("1.2.3.0/-1").IsValid());
    BOOST_CHECK(ResolveSubNet("1.2.3.0/32").IsValid());
    BOOST_CHECK(!ResolveSubNet("1.2.3.0/33").IsValid());
    BOOST_CHECK(!ResolveSubNet("1.2.3.0/300").IsValid());
    BOOST_CHECK(ResolveSubNet("1:2:3:4:5:6:7:8/0").IsValid());
    BOOST_CHECK(ResolveSubNet("1:2:3:4:5:6:7:8/33").IsValid());
    BOOST_CHECK(!ResolveSubNet("1:2:3:4:5:6:7:8/-1").IsValid());
    BOOST_CHECK(ResolveSubNet("1:2:3:4:5:6:7:8/128").IsValid());
    BOOST_CHECK(!ResolveSubNet("1:2:3:4:5:6:7:8/129").IsValid());
    BOOST_CHECK(!ResolveSubNet("fuzzy").IsValid());

    //CNetAddr constructor test
    BOOST_CHECK(CSubNet(ResolveIP("127.0.0.1")).IsValid());
    BOOST_CHECK(CSubNet(ResolveIP("127.0.0.1")).Match(ResolveIP("127.0.0.1")));
    BOOST_CHECK(!CSubNet(ResolveIP("127.0.0.1")).Match(ResolveIP("127.0.0.2")));
    BOOST_CHECK(CSubNet(ResolveIP("127.0.0.1")).ToString() == "127.0.0.1/32");

    CSubNet subnet = CSubNet(ResolveIP("1.2.3.4"), 32);
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.4/32");
    subnet = CSubNet(ResolveIP("1.2.3.4"), 8);
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/8");
    subnet = CSubNet(ResolveIP("1.2.3.4"), 0);
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/0");

    subnet = CSubNet(ResolveIP("1.2.3.4"), ResolveIP("255.255.255.255"));
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.4/32");
    subnet = CSubNet(ResolveIP("1.2.3.4"), ResolveIP("255.0.0.0"));
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/8");
    subnet = CSubNet(ResolveIP("1.2.3.4"), ResolveIP("0.0.0.0"));
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/0");

    BOOST_CHECK(CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).IsValid());
    BOOST_CHECK(CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).Match(ResolveIP("1:2:3:4:5:6:7:8")));
    BOOST_CHECK(!CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).Match(ResolveIP("1:2:3:4:5:6:7:9")));
    BOOST_CHECK(CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).ToString() == "1:2:3:4:5:6:7:8/128");
    // IPv4 address with IPv6 netmask or the other way around.
    BOOST_CHECK(!CSubNet(ResolveIP("1.1.1.1"), ResolveIP("ffff::")).IsValid());
    BOOST_CHECK(!CSubNet(ResolveIP("::1"), ResolveIP("255.0.0.0")).IsValid());

    // Create Non-IP subnets.

    const CNetAddr tor_addr{
        ResolveIP("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion")};

    subnet = CSubNet(tor_addr);
    BOOST_CHECK(subnet.IsValid());
    BOOST_CHECK_EQUAL(subnet.ToString(), tor_addr.ToStringAddr());
    BOOST_CHECK(subnet.Match(tor_addr));
    BOOST_CHECK(
        !subnet.Match(ResolveIP("kpgvmscirrdqpekbqjsvw5teanhatztpp2gl6eee4zkowvwfxwenqaid.onion")));
    BOOST_CHECK(!subnet.Match(ResolveIP("1.2.3.4")));

    BOOST_CHECK(!CSubNet(tor_addr, 200).IsValid());
    BOOST_CHECK(!CSubNet(tor_addr, ResolveIP("255.0.0.0")).IsValid());

    subnet = ResolveSubNet("1.2.3.4/255.255.255.255");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.4/32");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.254");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.4/31");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.252");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.4/30");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.248");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.0/29");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.240");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.0/28");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.224");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.0/27");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.192");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.0/26");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.128");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.0/25");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.3.0/24");
    subnet = ResolveSubNet("1.2.3.4/255.255.254.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.2.0/23");
    subnet = ResolveSubNet("1.2.3.4/255.255.252.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/22");
    subnet = ResolveSubNet("1.2.3.4/255.255.248.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/21");
    subnet = ResolveSubNet("1.2.3.4/255.255.240.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/20");
    subnet = ResolveSubNet("1.2.3.4/255.255.224.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/19");
    subnet = ResolveSubNet("1.2.3.4/255.255.192.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/18");
    subnet = ResolveSubNet("1.2.3.4/255.255.128.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/17");
    subnet = ResolveSubNet("1.2.3.4/255.255.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/16");
    subnet = ResolveSubNet("1.2.3.4/255.254.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.2.0.0/15");
    subnet = ResolveSubNet("1.2.3.4/255.252.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/14");
    subnet = ResolveSubNet("1.2.3.4/255.248.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/13");
    subnet = ResolveSubNet("1.2.3.4/255.240.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/12");
    subnet = ResolveSubNet("1.2.3.4/255.224.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/11");
    subnet = ResolveSubNet("1.2.3.4/255.192.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/10");
    subnet = ResolveSubNet("1.2.3.4/255.128.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/9");
    subnet = ResolveSubNet("1.2.3.4/255.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1.0.0.0/8");
    subnet = ResolveSubNet("1.2.3.4/254.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/7");
    subnet = ResolveSubNet("1.2.3.4/252.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/6");
    subnet = ResolveSubNet("1.2.3.4/248.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/5");
    subnet = ResolveSubNet("1.2.3.4/240.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/4");
    subnet = ResolveSubNet("1.2.3.4/224.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/3");
    subnet = ResolveSubNet("1.2.3.4/192.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/2");
    subnet = ResolveSubNet("1.2.3.4/128.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/1");
    subnet = ResolveSubNet("1.2.3.4/0.0.0.0");
    BOOST_CHECK_EQUAL(subnet.ToString(), "0.0.0.0/0");

    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1:2:3:4:5:6:7:8/128");
    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/ffff:0000:0000:0000:0000:0000:0000:0000");
    BOOST_CHECK_EQUAL(subnet.ToString(), "1::/16");
    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/0000:0000:0000:0000:0000:0000:0000:0000");
    BOOST_CHECK_EQUAL(subnet.ToString(), "::/0");
    // Invalid netmasks (with 1-bits after 0-bits)
    subnet = ResolveSubNet("1.2.3.4/255.255.232.0");
    BOOST_CHECK(!subnet.IsValid());
    subnet = ResolveSubNet("1.2.3.4/255.0.255.255");
    BOOST_CHECK(!subnet.IsValid());
    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/ffff:ffff:ffff:fffe:ffff:ffff:ffff:ff0f");
    BOOST_CHECK(!subnet.IsValid());
}

BOOST_AUTO_TEST_CASE(netbase_getgroup)
{
    NetGroupManager netgroupman{std::vector<bool>()}; // use /16
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("127.0.0.1")) == std::vector<unsigned char>({0})); // Local -> !Routable()
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("257.0.0.1")) == std::vector<unsigned char>({0})); // !Valid -> !Routable()
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("10.0.0.1")) == std::vector<unsigned char>({0})); // RFC1918 -> !Routable()
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("169.254.1.1")) == std::vector<unsigned char>({0})); // RFC3927 -> !Routable()
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("1.2.3.4")) == std::vector<unsigned char>({(unsigned char)NET_IPV4, 1, 2})); // IPv4
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("::FFFF:0:102:304")) == std::vector<unsigned char>({(unsigned char)NET_IPV4, 1, 2})); // RFC6145
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("64:FF9B::102:304")) == std::vector<unsigned char>({(unsigned char)NET_IPV4, 1, 2})); // RFC6052
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("2002:102:304:9999:9999:9999:9999:9999")) == std::vector<unsigned char>({(unsigned char)NET_IPV4, 1, 2})); // RFC3964
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("2001:0:9999:9999:9999:9999:FEFD:FCFB")) == std::vector<unsigned char>({(unsigned char)NET_IPV4, 1, 2})); // RFC4380
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("2001:470:abcd:9999:9999:9999:9999:9999")) == std::vector<unsigned char>({(unsigned char)NET_IPV6, 32, 1, 4, 112, 175})); //he.net
    BOOST_CHECK(netgroupman.GetGroup(ResolveIP("2001:2001:9999:9999:9999:9999:9999:9999")) == std::vector<unsigned char>({(unsigned char)NET_IPV6, 32, 1, 32, 1})); //IPv6

    // baz.net sha256 hash: 12929400eb4607c4ac075f087167e75286b179c693eb059a01774b864e8fe505
    std::vector<unsigned char> internal_group = {NET_INTERNAL, 0x12, 0x92, 0x94, 0x00, 0xeb, 0x46, 0x07, 0xc4, 0xac, 0x07};
    BOOST_CHECK(netgroupman.GetGroup(CreateInternal("baz.net")) == internal_group);
}

BOOST_AUTO_TEST_CASE(netbase_parsenetwork)
{
    BOOST_CHECK_EQUAL(ParseNetwork("ipv4"), NET_IPV4);
    BOOST_CHECK_EQUAL(ParseNetwork("ipv6"), NET_IPV6);
    BOOST_CHECK_EQUAL(ParseNetwork("onion"), NET_ONION);
    BOOST_CHECK_EQUAL(ParseNetwork("tor"), NET_ONION);
    BOOST_CHECK_EQUAL(ParseNetwork("cjdns"), NET_CJDNS);

    BOOST_CHECK_EQUAL(ParseNetwork("IPv4"), NET_IPV4);
    BOOST_CHECK_EQUAL(ParseNetwork("IPv6"), NET_IPV6);
    BOOST_CHECK_EQUAL(ParseNetwork("ONION"), NET_ONION);
    BOOST_CHECK_EQUAL(ParseNetwork("TOR"), NET_ONION);
    BOOST_CHECK_EQUAL(ParseNetwork("CJDNS"), NET_CJDNS);

    BOOST_CHECK_EQUAL(ParseNetwork(":)"), NET_UNROUTABLE);
    BOOST_CHECK_EQUAL(ParseNetwork("tÖr"), NET_UNROUTABLE);
    BOOST_CHECK_EQUAL(ParseNetwork("\xfe\xff"), NET_UNROUTABLE);
    BOOST_CHECK_EQUAL(ParseNetwork(""), NET_UNROUTABLE);
}

BOOST_AUTO_TEST_CASE(netpermissions_test)
{
    bilingual_str error;
    NetWhitebindPermissions whitebindPermissions;
    NetWhitelistPermissions whitelistPermissions;

    // Detect invalid white bind
    BOOST_CHECK(!NetWhitebindPermissions::TryParse("", whitebindPermissions, error));
    BOOST_CHECK(error.original.find("Cannot resolve -whitebind address") != std::string::npos);
    BOOST_CHECK(!NetWhitebindPermissions::TryParse("127.0.0.1", whitebindPermissions, error));
    BOOST_CHECK(error.original.find("Need to specify a port with -whitebind") != std::string::npos);
    BOOST_CHECK(!NetWhitebindPermissions::TryParse("", whitebindPermissions, error));

    // If no permission flags, assume backward compatibility
    BOOST_CHECK(NetWhitebindPermissions::TryParse("1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK(error.empty());
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::Implicit);
    BOOST_CHECK(NetPermissions::HasFlag(whitebindPermissions.m_flags, NetPermissionFlags::Implicit));
    NetPermissions::ClearFlag(whitebindPermissions.m_flags, NetPermissionFlags::Implicit);
    BOOST_CHECK(!NetPermissions::HasFlag(whitebindPermissions.m_flags, NetPermissionFlags::Implicit));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::None);
    NetPermissions::AddFlag(whitebindPermissions.m_flags, NetPermissionFlags::Implicit);
    BOOST_CHECK(NetPermissions::HasFlag(whitebindPermissions.m_flags, NetPermissionFlags::Implicit));

    // Can set one permission
    BOOST_CHECK(!NetWhitebindPermissions::TryParse("bloom@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK(error.original.find("Bloom") != std::string::npos);
    BOOST_CHECK(NetWhitebindPermissions::TryParse("mempool@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::Mempool);
    BOOST_CHECK(NetWhitebindPermissions::TryParse("@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::None);

    NetWhitebindPermissions noban, noban_download, download_noban, download;

    // "noban" implies "download"
    BOOST_REQUIRE(NetWhitebindPermissions::TryParse("noban@1.2.3.4:32", noban, error));
    BOOST_CHECK_EQUAL(noban.m_flags, NetPermissionFlags::NoBan);
    BOOST_CHECK(NetPermissions::HasFlag(noban.m_flags, NetPermissionFlags::Download));
    BOOST_CHECK(NetPermissions::HasFlag(noban.m_flags, NetPermissionFlags::NoBan));

    // "noban,download" is equivalent to "noban"
    BOOST_REQUIRE(NetWhitebindPermissions::TryParse("noban,download@1.2.3.4:32", noban_download, error));
    BOOST_CHECK_EQUAL(noban_download.m_flags, noban.m_flags);

    // "download,noban" is equivalent to "noban"
    BOOST_REQUIRE(NetWhitebindPermissions::TryParse("download,noban@1.2.3.4:32", download_noban, error));
    BOOST_CHECK_EQUAL(download_noban.m_flags, noban.m_flags);

    // "download" excludes (does not imply) "noban"
    BOOST_REQUIRE(NetWhitebindPermissions::TryParse("download@1.2.3.4:32", download, error));
    BOOST_CHECK_EQUAL(download.m_flags, NetPermissionFlags::Download);
    BOOST_CHECK(NetPermissions::HasFlag(download.m_flags, NetPermissionFlags::Download));
    BOOST_CHECK(!NetPermissions::HasFlag(download.m_flags, NetPermissionFlags::NoBan));

    // Happy path, can parse flags
    BOOST_CHECK(NetWhitebindPermissions::TryParse("forcerelay@1.2.3.4:32", whitebindPermissions, error));
    // forcerelay should also activate the relay permission
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::ForceRelay | NetPermissionFlags::Relay);
    BOOST_CHECK(NetWhitebindPermissions::TryParse("relay,noban@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::Relay | NetPermissionFlags::NoBan);
    BOOST_CHECK(NetWhitebindPermissions::TryParse("forcerelay,noban@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK(NetWhitebindPermissions::TryParse("all@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::All);

    // Allow dups
    BOOST_CHECK(NetWhitebindPermissions::TryParse("relay,noban,noban@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::Relay | NetPermissionFlags::NoBan | NetPermissionFlags::Download); // "noban" implies "download"

    // Allow empty
    BOOST_CHECK(NetWhitebindPermissions::TryParse("relay,,noban@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::Relay | NetPermissionFlags::NoBan);
    BOOST_CHECK(NetWhitebindPermissions::TryParse(",@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::None);
    BOOST_CHECK(NetWhitebindPermissions::TryParse(",,@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK_EQUAL(whitebindPermissions.m_flags, NetPermissionFlags::None);

    // Detect invalid flag
    BOOST_CHECK(!NetWhitebindPermissions::TryParse("forcerelay,oopsie@1.2.3.4:32", whitebindPermissions, error));
    BOOST_CHECK(error.original.find("Invalid P2P permission") != std::string::npos);

    // Check netmask error
    BOOST_CHECK(!NetWhitelistPermissions::TryParse("forcerelay,noban@1.2.3.4:32", whitelistPermissions, error));
    BOOST_CHECK(error.original.find("Invalid netmask specified in -whitelist") != std::string::npos);

    // Happy path for whitelist parsing
    BOOST_CHECK(NetWhitelistPermissions::TryParse("noban@1.2.3.4", whitelistPermissions, error));
    BOOST_CHECK_EQUAL(whitelistPermissions.m_flags, NetPermissionFlags::NoBan);
    BOOST_CHECK(NetPermissions::HasFlag(whitelistPermissions.m_flags, NetPermissionFlags::NoBan));

    BOOST_CHECK(NetWhitelistPermissions::TryParse("forcerelay,noban,relay@1.2.3.4/32", whitelistPermissions, error));
    BOOST_CHECK_EQUAL(whitelistPermissions.m_flags, NetPermissionFlags::ForceRelay | NetPermissionFlags::NoBan | NetPermissionFlags::Relay);
    BOOST_CHECK(error.empty());
    BOOST_CHECK_EQUAL(whitelistPermissions.m_subnet.ToString(), "1.2.3.4/32");
    BOOST_CHECK(NetWhitelistPermissions::TryParse("forcerelay,noban,relay,mempool@1.2.3.4/32", whitelistPermissions, error));

    const auto strings = NetPermissions::ToStrings(NetPermissionFlags::All);
    BOOST_CHECK_EQUAL(strings.size(), 6U);
    BOOST_CHECK(std::find(strings.begin(), strings.end(), "forcerelay") != strings.end());
    BOOST_CHECK(std::find(strings.begin(), strings.end(), "relay") != strings.end());
    BOOST_CHECK(std::find(strings.begin(), strings.end(), "noban") != strings.end());
    BOOST_CHECK(std::find(strings.begin(), strings.end(), "mempool") != strings.end());
    BOOST_CHECK(std::find(strings.begin(), strings.end(), "download") != strings.end());
    BOOST_CHECK(std::find(strings.begin(), strings.end(), "addr") != strings.end());
}

BOOST_AUTO_TEST_CASE(netbase_dont_resolve_strings_with_embedded_nul_characters)
{
    CNetAddr addr;
    BOOST_CHECK(LookupHost("127.0.0.1"s, addr, false));
    BOOST_CHECK(!LookupHost("127.0.0.1\0"s, addr, false));
    BOOST_CHECK(!LookupHost("127.0.0.1\0example.com"s, addr, false));
    BOOST_CHECK(!LookupHost("127.0.0.1\0example.com\0"s, addr, false));
    CSubNet ret;
    BOOST_CHECK(LookupSubNet("1.2.3.0/24"s, ret));
    BOOST_CHECK(!LookupSubNet("1.2.3.0/24\0"s, ret));
    BOOST_CHECK(!LookupSubNet("1.2.3.0/24\0example.com"s, ret));
    BOOST_CHECK(!LookupSubNet("1.2.3.0/24\0example.com\0"s, ret));
    BOOST_CHECK(LookupSubNet("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"s, ret));
    BOOST_CHECK(!LookupSubNet("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion\0"s, ret));
    BOOST_CHECK(!LookupSubNet("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion\0example.com"s, ret));
    BOOST_CHECK(!LookupSubNet("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion\0example.com\0"s, ret));
}

// Since CNetAddr (un)ser is tested separately in net_tests.cpp here we only
// try a few edge cases for port, service flags and time.

static const std::vector<CAddress> fixture_addresses({
    CAddress{
        CService(CNetAddr(in6_addr(IN6ADDR_LOOPBACK_INIT)), 0 /* port */),
        NODE_NONE,
        NodeSeconds{0x4966bc61s}, /* Fri Jan  9 02:54:25 UTC 2009 */
    },
    CAddress{
        CService(CNetAddr(in6_addr(IN6ADDR_LOOPBACK_INIT)), 0x00f1 /* port */),
        NODE_NETWORK,
        NodeSeconds{0x83766279s}, /* Tue Nov 22 11:22:33 UTC 2039 */
    },
    CAddress{
        CService(CNetAddr(in6_addr(IN6ADDR_LOOPBACK_INIT)), 0xf1f2 /* port */),
        static_cast<ServiceFlags>(NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_NETWORK_LIMITED),
        NodeSeconds{0xffffffffs}, /* Sun Feb  7 06:28:15 UTC 2106 */
    },
});

// fixture_addresses should equal to this when serialized in V1 format.
// When this is unserialized from V1 format it should equal to fixture_addresses.
static constexpr const char* stream_addrv1_hex =
    "03" // number of entries

    "61bc6649"                         // time, Fri Jan  9 02:54:25 UTC 2009
    "0000000000000000"                 // service flags, NODE_NONE
    "00000000000000000000000000000001" // address, fixed 16 bytes (IPv4 embedded in IPv6)
    "0000"                             // port

    "79627683"                         // time, Tue Nov 22 11:22:33 UTC 2039
    "0100000000000000"                 // service flags, NODE_NETWORK
    "00000000000000000000000000000001" // address, fixed 16 bytes (IPv6)
    "00f1"                             // port

    "ffffffff"                         // time, Sun Feb  7 06:28:15 UTC 2106
    "4804000000000000"                 // service flags, NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_NETWORK_LIMITED
    "00000000000000000000000000000001" // address, fixed 16 bytes (IPv6)
    "f1f2";                            // port

// fixture_addresses should equal to this when serialized in V2 format.
// When this is unserialized from V2 format it should equal to fixture_addresses.
static constexpr const char* stream_addrv2_hex =
    "03" // number of entries

    "61bc6649"                         // time, Fri Jan  9 02:54:25 UTC 2009
    "00"                               // service flags, COMPACTSIZE(NODE_NONE)
    "02"                               // network id, IPv6
    "10"                               // address length, COMPACTSIZE(16)
    "00000000000000000000000000000001" // address
    "0000"                             // port

    "79627683"                         // time, Tue Nov 22 11:22:33 UTC 2039
    "01"                               // service flags, COMPACTSIZE(NODE_NETWORK)
    "02"                               // network id, IPv6
    "10"                               // address length, COMPACTSIZE(16)
    "00000000000000000000000000000001" // address
    "00f1"                             // port

    "ffffffff"                         // time, Sun Feb  7 06:28:15 UTC 2106
    "fd4804"                           // service flags, COMPACTSIZE(NODE_WITNESS | NODE_COMPACT_FILTERS | NODE_NETWORK_LIMITED)
    "02"                               // network id, IPv6
    "10"                               // address length, COMPACTSIZE(16)
    "00000000000000000000000000000001" // address
    "f1f2";                            // port

BOOST_AUTO_TEST_CASE(caddress_serialize_v1)
{
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);

    s << fixture_addresses;
    BOOST_CHECK_EQUAL(HexStr(s), stream_addrv1_hex);
}

BOOST_AUTO_TEST_CASE(caddress_unserialize_v1)
{
    CDataStream s(ParseHex(stream_addrv1_hex), SER_NETWORK, PROTOCOL_VERSION);
    std::vector<CAddress> addresses_unserialized;

    s >> addresses_unserialized;
    BOOST_CHECK(fixture_addresses == addresses_unserialized);
}

BOOST_AUTO_TEST_CASE(caddress_serialize_v2)
{
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);

    s << fixture_addresses;
    BOOST_CHECK_EQUAL(HexStr(s), stream_addrv2_hex);
}

BOOST_AUTO_TEST_CASE(caddress_unserialize_v2)
{
    CDataStream s(ParseHex(stream_addrv2_hex), SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    std::vector<CAddress> addresses_unserialized;

    s >> addresses_unserialized;
    BOOST_CHECK(fixture_addresses == addresses_unserialized);
}

BOOST_AUTO_TEST_CASE(isbadport)
{
    BOOST_CHECK(IsBadPort(1));
    BOOST_CHECK(IsBadPort(22));
    BOOST_CHECK(IsBadPort(6000));

    BOOST_CHECK(!IsBadPort(80));
    BOOST_CHECK(!IsBadPort(443));
    BOOST_CHECK(!IsBadPort(8333));

    // Check all ports; keep in sync with IsBadPort() in netbase.cpp and doc/p2p-bad-ports.md.
    size_t total_bad_ports{0};
    for (uint16_t port = std::numeric_limits<uint16_t>::max(); port > 0; --port) {
        if (IsBadPort(port)) {
            ++total_bad_ports;
        }
    }
    BOOST_CHECK_EQUAL(total_bad_ports, 85);
}

class GreetingSock : public Sock
{
public:
    GreetingSock(std::string reply, bool close_immediately, bool block)
        : m_reply(std::move(reply)), m_close(close_immediately), m_block(block)
    {
        m_socket = INVALID_SOCKET - 1;
    }
    ~GreetingSock() override { m_socket = INVALID_SOCKET; }
    GreetingSock& operator=(Sock&&) override
    {
        assert(false && "Move of Sock into GreetingSock not allowed.");
        return *this;
    }
    ssize_t Send(const void* data, size_t len, int) const override
    {
        m_sent.append(static_cast<const char*>(data), len);
        return static_cast<ssize_t>(len);
    }
    ssize_t Recv(void* buf, size_t len, int) const override
    {
        if (m_block) {
#ifdef WIN32
            WSASetLastError(WSAEWOULDBLOCK);
#else
            errno = EWOULDBLOCK;
#endif
            return -1;
        }
        if (m_close || m_offset >= m_reply.size()) return 0;
        const size_t n = std::min(len, m_reply.size() - m_offset);
        std::memcpy(buf, m_reply.data() + m_offset, n);
        m_offset += n;
        return static_cast<ssize_t>(n);
    }
    bool Wait(std::chrono::milliseconds, Event, Event* occurred) const override
    {
        if (occurred != nullptr) *occurred = 0;
        return !m_block;
    }
    const std::string& sent() const { return m_sent; }

private:
    std::string m_reply;
    bool m_close;
    bool m_block;
    mutable std::string m_sent;
    mutable size_t m_offset{0};
};

BOOST_AUTO_TEST_CASE(socks5_greeting_does_not_connect)
{
    GreetingSock ok(std::string("\x05\x00", 2), false, false);
    const Socks5GreetingResult success = Socks5Greeting(ok, nullptr, 50);
    BOOST_CHECK(success.ok);
    BOOST_CHECK_EQUAL(success.method, 0x00);
    BOOST_CHECK_EQUAL(ok.sent(), std::string("\x05\x01\x00", 3));

    GreetingSock rejected(std::string("\x04\x00", 2), false, false);
    const Socks5GreetingResult bad_version = Socks5Greeting(rejected, nullptr, 50);
    BOOST_CHECK(!bad_version.ok);
    BOOST_CHECK(bad_version.version_rejected);

    GreetingSock closed("", true, false);
    const Socks5GreetingResult dropped = Socks5Greeting(closed, nullptr, 50);
    BOOST_CHECK(!dropped.ok);
    BOOST_CHECK(dropped.io_failed);

    GreetingSock silent("", false, true);
    const Socks5GreetingResult timed_out = Socks5Greeting(silent, nullptr, 30);
    BOOST_CHECK(!timed_out.ok);
    BOOST_CHECK(timed_out.io_failed);
    BOOST_CHECK_EQUAL(silent.sent(), std::string("\x05\x01\x00", 3));
}

BOOST_AUTO_TEST_CASE(netbase_rfc9637_documentation_prefix)
{
    const CNetAddr first = ResolveIP("3fff::");
    const CNetAddr last = ResolveIP("3fff:0fff:ffff:ffff:ffff:ffff:ffff:ffff");
    const CNetAddr just_after = ResolveIP("3fff:1000::");
    const CNetAddr just_before = ResolveIP("3ffe:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    const CNetAddr rfc3849 = ResolveIP("2001:db8::");
    const CNetAddr rfc3849_last = ResolveIP("2001:db8:ffff:ffff:ffff:ffff:ffff:ffff");
    const CNetAddr outside_3849 = ResolveIP("2001:db9::");
    const CNetAddr loopback = ResolveIP("::1");
    const CNetAddr link_local = ResolveIP("fe80::1");
    const CNetAddr global = ResolveIP("2001:4860:4860::8888");
    const CNetAddr unique_local = ResolveIP("fc00::");
    const CNetAddr sixto4 = ResolveIP("2002::1");
    const CNetAddr teredo = ResolveIP("2001::2");
    const CNetAddr nat64 = ResolveIP("64:ff9b::");
    const CNetAddr ipv4 = ResolveIP("8.8.8.8");

    for (const CNetAddr& doc : {first, last}) {
        BOOST_CHECK(doc.IsIPv6());
        BOOST_CHECK(doc.IsRFC9637());
        BOOST_CHECK(!doc.IsRFC3849());
        BOOST_CHECK(!doc.IsValid());
        BOOST_CHECK(!doc.IsRoutable());
        BOOST_CHECK(!doc.IsLocal());
        BOOST_CHECK(doc.GetNetwork() == NET_UNROUTABLE);
        BOOST_CHECK(doc.IsAddrV1Compatible());
    }

    BOOST_CHECK(!just_after.IsRFC9637());
    BOOST_CHECK(just_after.IsValid());
    BOOST_CHECK(just_after.IsRoutable());
    BOOST_CHECK(just_after.IsIPv6());
    BOOST_CHECK(just_after.GetNetwork() == NET_IPV6);

    BOOST_CHECK(!just_before.IsRFC9637());
    BOOST_CHECK(just_before.IsValid());
    BOOST_CHECK(just_before.IsRoutable());
    BOOST_CHECK(just_before.GetNetwork() == NET_IPV6);

    BOOST_CHECK(rfc3849.IsRFC3849());
    BOOST_CHECK(rfc3849_last.IsRFC3849());
    BOOST_CHECK(!rfc3849.IsRFC9637());
    BOOST_CHECK(!rfc3849.IsValid());
    BOOST_CHECK(!rfc3849.IsRoutable());
    BOOST_CHECK(!outside_3849.IsRFC3849());
    BOOST_CHECK(!outside_3849.IsRFC9637());
    BOOST_CHECK(outside_3849.IsRoutable());

    BOOST_CHECK(loopback.IsIPv6());
    BOOST_CHECK(loopback.IsLocal());
    BOOST_CHECK(loopback.IsValid());
    BOOST_CHECK(!loopback.IsRoutable());
    BOOST_CHECK(!loopback.IsRFC9637());
    BOOST_CHECK(loopback.GetNetwork() == NET_UNROUTABLE);

    BOOST_CHECK(link_local.IsRFC4862());
    BOOST_CHECK(link_local.IsValid());
    BOOST_CHECK(!link_local.IsRoutable());
    BOOST_CHECK(!link_local.IsRFC9637());

    BOOST_CHECK(global.IsIPv6());
    BOOST_CHECK(global.IsValid());
    BOOST_CHECK(global.IsRoutable());
    BOOST_CHECK(!global.IsRFC9637());
    BOOST_CHECK(!global.IsRFC3849());
    BOOST_CHECK(global.GetNetwork() == NET_IPV6);

    BOOST_CHECK(unique_local.IsRFC4193());
    BOOST_CHECK(!unique_local.IsRoutable());
    BOOST_CHECK(!unique_local.IsRFC9637());
    BOOST_CHECK(sixto4.IsRFC3964());
    BOOST_CHECK(sixto4.IsRoutable());
    BOOST_CHECK(teredo.IsRFC4380());
    BOOST_CHECK(teredo.IsRoutable());
    BOOST_CHECK(nat64.IsRFC6052());
    BOOST_CHECK(nat64.IsValid());
    BOOST_CHECK(!nat64.IsRFC9637());
    BOOST_CHECK(ipv4.IsIPv4());
    BOOST_CHECK(!ipv4.IsRFC9637());
    BOOST_CHECK(ipv4.IsRoutable());

    // ADDRv2 network id for IPv6 stays 2. Invalid documentation addresses are
    // still IPv6 values; validity is a separate check from serialization.
    // Layout: uint32 time 100000000, compactsize services 0, network id 2, length 16.
    const std::string addrv2_ipv6_prefix = "00e1f505000210";
    CDataStream global_stream(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    global_stream << CAddress(CService(global, 8760), NODE_NONE);
    CDataStream doc_stream(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    doc_stream << CAddress(CService(first, 8760), NODE_NONE);
    BOOST_CHECK_EQUAL(HexStr(global_stream).substr(0, addrv2_ipv6_prefix.size()), addrv2_ipv6_prefix);
    BOOST_CHECK_EQUAL(HexStr(doc_stream).substr(0, addrv2_ipv6_prefix.size()), addrv2_ipv6_prefix);
}

BOOST_AUTO_TEST_CASE(netbase_addrconfig_fallback)
{
    constexpr int kFailed = 1;
    const CNetAddr ipv4 = ResolveIP("192.0.2.1");
    const CNetAddr ipv6 = ResolveIP("2001:4860:4860::8888");
    BOOST_REQUIRE(ipv4.IsIPv4());
    BOOST_REQUIRE(ipv6.IsIPv6());

    {
        int calls = 0;
        const std::vector<CNetAddr> got = LookupWithAddrConfigFallback(true, [&](int flags) {
            ++calls;
            BOOST_CHECK((flags & AI_ADDRCONFIG) == AI_ADDRCONFIG);
            return AddrInfoAttempt{0, {ipv4}};
        });
        BOOST_CHECK_EQUAL(calls, 1);
        BOOST_REQUIRE_EQUAL(got.size(), 1U);
        BOOST_CHECK(got[0] == ipv4);
        BOOST_CHECK(got[0].IsIPv4());
    }

    {
        int calls = 0;
        const std::vector<CNetAddr> got = LookupWithAddrConfigFallback(true, [&](int flags) {
            ++calls;
            BOOST_CHECK((flags & AI_ADDRCONFIG) == AI_ADDRCONFIG);
            return AddrInfoAttempt{0, {ipv6}};
        });
        BOOST_CHECK_EQUAL(calls, 1);
        BOOST_REQUIRE_EQUAL(got.size(), 1U);
        BOOST_CHECK(got[0] == ipv6);
        BOOST_CHECK(got[0].IsIPv6());
    }

    {
        // IPv4-only configured host: AI_ADDRCONFIG succeeds and returns A only.
        int calls = 0;
        const std::vector<CNetAddr> got = LookupWithAddrConfigFallback(true, [&](int flags) {
            ++calls;
            BOOST_CHECK((flags & AI_ADDRCONFIG) == AI_ADDRCONFIG);
            return AddrInfoAttempt{0, {ipv4}};
        });
        BOOST_CHECK_EQUAL(calls, 1);
        BOOST_REQUIRE_EQUAL(got.size(), 1U);
        BOOST_CHECK(got[0].IsIPv4());
        BOOST_CHECK(!got[0].IsIPv6());
    }

    {
        // No usable global IPv6: the flagged lookup fails and the retry returns both families.
        std::vector<int> seen;
        const std::vector<CNetAddr> got = LookupWithAddrConfigFallback(true, [&](int flags) {
            seen.push_back(flags);
            if ((flags & AI_ADDRCONFIG) == AI_ADDRCONFIG) {
                return AddrInfoAttempt{kFailed, {ipv4}};
            }
            return AddrInfoAttempt{0, {ipv4, ipv6}};
        });
        BOOST_REQUIRE_EQUAL(seen.size(), 2U);
        BOOST_CHECK((seen[0] & AI_ADDRCONFIG) == AI_ADDRCONFIG);
        BOOST_CHECK((seen[1] & AI_ADDRCONFIG) == 0);
        BOOST_REQUIRE_EQUAL(got.size(), 2U);
        BOOST_CHECK(got[0].IsIPv4());
        BOOST_CHECK(got[1].IsIPv6());
    }

    {
        std::vector<int> seen;
        const std::vector<CNetAddr> got = LookupWithAddrConfigFallback(true, [&](int flags) {
            seen.push_back(flags);
            return AddrInfoAttempt{kFailed, {}};
        });
        BOOST_CHECK_EQUAL(seen.size(), 2U);
        BOOST_CHECK((seen[0] & AI_ADDRCONFIG) == AI_ADDRCONFIG);
        BOOST_CHECK((seen[1] & AI_ADDRCONFIG) == 0);
        BOOST_CHECK(got.empty());
    }

    {
        // Numeric parsing must not turn a failure into a second, non-numeric lookup.
        int calls = 0;
        const std::vector<CNetAddr> got = LookupWithAddrConfigFallback(false, [&](int flags) {
            ++calls;
            BOOST_CHECK((flags & AI_NUMERICHOST) == AI_NUMERICHOST);
            BOOST_CHECK((flags & AI_ADDRCONFIG) == 0);
            return AddrInfoAttempt{kFailed, {ipv4}};
        });
        BOOST_CHECK_EQUAL(calls, 1);
        BOOST_CHECK(got.empty());
    }

    // Real getaddrinfo on numeric literals. No public DNS.
    const std::vector<CNetAddr> real_v4 = WrappedGetAddrInfo("192.0.2.1", true);
    BOOST_REQUIRE_EQUAL(real_v4.size(), 1U);
    BOOST_CHECK(real_v4[0].IsIPv4());
    BOOST_CHECK_EQUAL(real_v4[0].ToStringAddr(), "192.0.2.1");

    const std::vector<CNetAddr> real_v6 = WrappedGetAddrInfo("2001:db8::1", true);
    BOOST_REQUIRE_EQUAL(real_v6.size(), 1U);
    BOOST_CHECK(real_v6[0].IsIPv6());
    BOOST_CHECK_EQUAL(real_v6[0].ToStringAddr(), "2001:db8::1");

    const std::vector<CNetAddr> real_loopback = WrappedGetAddrInfo("::1", false);
    BOOST_REQUIRE_EQUAL(real_loopback.size(), 1U);
    BOOST_CHECK(real_loopback[0].IsLocal());

    BOOST_CHECK(WrappedGetAddrInfo("not a literal", false).empty());
    CNetAddr ipv4_check;
    BOOST_CHECK(LookupHost("192.0.2.1", ipv4_check, false));
    BOOST_CHECK(ipv4_check.IsIPv4());
    CNetAddr ipv6_check;
    BOOST_CHECK(LookupHost("2001:4860:4860::8888", ipv6_check, false));
    BOOST_CHECK(ipv6_check.IsIPv6());
    BOOST_CHECK(ipv6_check.IsRoutable());
}

BOOST_AUTO_TEST_SUITE_END()
