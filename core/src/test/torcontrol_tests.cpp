// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//

#include <boost/test/unit_test.hpp>

#include <chainparamsbase.h>
#include <netbase.h>
#include <tor_mode.h>
#include <torcontrol.h>

#include <util/readwritefile.h>
#include <util/system.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <compat/compat.h>

#ifndef WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#endif


std::pair<std::string, std::string> SplitTorReplyLine(const std::string& s);
std::map<std::string, std::string> ParseTorReplyMapping(const std::string& s);


BOOST_AUTO_TEST_SUITE(torcontrol_tests)

static void CheckSplitTorReplyLine(std::string input, std::string command, std::string args)
{
    auto ret = SplitTorReplyLine(input);
    BOOST_CHECK_EQUAL(ret.first, command);
    BOOST_CHECK_EQUAL(ret.second, args);
}

BOOST_AUTO_TEST_CASE(util_SplitTorReplyLine)
{
    // Data we should receive during normal usage
    CheckSplitTorReplyLine(
        "PROTOCOLINFO PIVERSION",
        "PROTOCOLINFO", "PIVERSION");
    CheckSplitTorReplyLine(
        "AUTH METHODS=COOKIE,SAFECOOKIE COOKIEFILE=\"/home/x/.tor/control_auth_cookie\"",
        "AUTH", "METHODS=COOKIE,SAFECOOKIE COOKIEFILE=\"/home/x/.tor/control_auth_cookie\"");
    CheckSplitTorReplyLine(
        "AUTH METHODS=NULL",
        "AUTH", "METHODS=NULL");
    CheckSplitTorReplyLine(
        "AUTH METHODS=HASHEDPASSWORD",
        "AUTH", "METHODS=HASHEDPASSWORD");
    CheckSplitTorReplyLine(
        "VERSION Tor=\"0.2.9.8 (git-a0df013ea241b026)\"",
        "VERSION", "Tor=\"0.2.9.8 (git-a0df013ea241b026)\"");
    CheckSplitTorReplyLine(
        "AUTHCHALLENGE SERVERHASH=aaaa SERVERNONCE=bbbb",
        "AUTHCHALLENGE", "SERVERHASH=aaaa SERVERNONCE=bbbb");

    // Other valid inputs
    CheckSplitTorReplyLine("COMMAND", "COMMAND", "");
    CheckSplitTorReplyLine("COMMAND SOME  ARGS", "COMMAND", "SOME  ARGS");

    // These inputs are valid because PROTOCOLINFO accepts an OtherLine that is
    // just an OptArguments, which enables multiple spaces to be present
    // between the command and arguments.
    CheckSplitTorReplyLine("COMMAND  ARGS", "COMMAND", " ARGS");
    CheckSplitTorReplyLine("COMMAND   EVEN+more  ARGS", "COMMAND", "  EVEN+more  ARGS");
}

static void CheckParseTorReplyMapping(std::string input, std::map<std::string,std::string> expected)
{
    auto ret = ParseTorReplyMapping(input);
    BOOST_CHECK_EQUAL(ret.size(), expected.size());
    auto r_it = ret.begin();
    auto e_it = expected.begin();
    while (r_it != ret.end() && e_it != expected.end()) {
        BOOST_CHECK_EQUAL(r_it->first, e_it->first);
        BOOST_CHECK_EQUAL(r_it->second, e_it->second);
        r_it++;
        e_it++;
    }
}

BOOST_AUTO_TEST_CASE(util_ParseTorReplyMapping)
{
    // Data we should receive during normal usage
    CheckParseTorReplyMapping(
        "METHODS=COOKIE,SAFECOOKIE COOKIEFILE=\"/home/x/.tor/control_auth_cookie\"", {
            {"METHODS", "COOKIE,SAFECOOKIE"},
            {"COOKIEFILE", "/home/x/.tor/control_auth_cookie"},
        });
    CheckParseTorReplyMapping(
        "METHODS=NULL", {
            {"METHODS", "NULL"},
        });
    CheckParseTorReplyMapping(
        "METHODS=HASHEDPASSWORD", {
            {"METHODS", "HASHEDPASSWORD"},
        });
    CheckParseTorReplyMapping(
        "Tor=\"0.2.9.8 (git-a0df013ea241b026)\"", {
            {"Tor", "0.2.9.8 (git-a0df013ea241b026)"},
        });
    CheckParseTorReplyMapping(
        "SERVERHASH=aaaa SERVERNONCE=bbbb", {
            {"SERVERHASH", "aaaa"},
            {"SERVERNONCE", "bbbb"},
        });
    CheckParseTorReplyMapping(
        "ServiceID=exampleonion1234", {
            {"ServiceID", "exampleonion1234"},
        });
    CheckParseTorReplyMapping(
        "PrivateKey=RSA1024:BLOB", {
            {"PrivateKey", "RSA1024:BLOB"},
        });
    CheckParseTorReplyMapping(
        "ClientAuth=bob:BLOB", {
            {"ClientAuth", "bob:BLOB"},
        });

    // Other valid inputs
    CheckParseTorReplyMapping(
        "Foo=Bar=Baz Spam=Eggs", {
            {"Foo", "Bar=Baz"},
            {"Spam", "Eggs"},
        });
    CheckParseTorReplyMapping(
        "Foo=\"Bar=Baz\"", {
            {"Foo", "Bar=Baz"},
        });
    CheckParseTorReplyMapping(
        "Foo=\"Bar Baz\"", {
            {"Foo", "Bar Baz"},
        });

    // Escapes
    CheckParseTorReplyMapping(
        "Foo=\"Bar\\ Baz\"", {
            {"Foo", "Bar Baz"},
        });
    CheckParseTorReplyMapping(
        "Foo=\"Bar\\Baz\"", {
            {"Foo", "BarBaz"},
        });
    CheckParseTorReplyMapping(
        "Foo=\"Bar\\@Baz\"", {
            {"Foo", "Bar@Baz"},
        });
    CheckParseTorReplyMapping(
        "Foo=\"Bar\\\"Baz\" Spam=\"\\\"Eggs\\\"\"", {
            {"Foo", "Bar\"Baz"},
            {"Spam", "\"Eggs\""},
        });
    CheckParseTorReplyMapping(
        "Foo=\"Bar\\\\Baz\"", {
            {"Foo", "Bar\\Baz"},
        });

    // C escapes
    CheckParseTorReplyMapping(
        "Foo=\"Bar\\nBaz\\t\" Spam=\"\\rEggs\" Octals=\"\\1a\\11\\17\\18\\81\\377\\378\\400\\2222\" Final=Check", {
            {"Foo", "Bar\nBaz\t"},
            {"Spam", "\rEggs"},
            {"Octals", "\1a\11\17\1" "881\377\37" "8\40" "0\222" "2"},
            {"Final", "Check"},
        });
    CheckParseTorReplyMapping(
        "Valid=Mapping Escaped=\"Escape\\\\\"", {
            {"Valid", "Mapping"},
            {"Escaped", "Escape\\"},
        });
    CheckParseTorReplyMapping(
        "Valid=Mapping Bare=\"Escape\\\"", {});
    CheckParseTorReplyMapping(
        "OneOctal=\"OneEnd\\1\" TwoOctal=\"TwoEnd\\11\"", {
            {"OneOctal", "OneEnd\1"},
            {"TwoOctal", "TwoEnd\11"},
        });

    // Special handling for null case
    // (needed because string comparison reads the null as end-of-string)
    auto ret = ParseTorReplyMapping("Null=\"\\0\"");
    BOOST_CHECK_EQUAL(ret.size(), 1U);
    auto r_it = ret.begin();
    BOOST_CHECK_EQUAL(r_it->first, "Null");
    BOOST_CHECK_EQUAL(r_it->second.size(), 1U);
    BOOST_CHECK_EQUAL(r_it->second[0], '\0');

    // A more complex valid grammar. PROTOCOLINFO accepts a VersionLine that
    // takes a key=value pair followed by an OptArguments, making this valid.
    // Because an OptArguments contains no semantic data, there is no point in
    // parsing it.
    CheckParseTorReplyMapping(
        "SOME=args,here MORE optional=arguments  here", {
            {"SOME", "args,here"},
        });

    // Inputs that are effectively invalid under the target grammar.
    // PROTOCOLINFO accepts an OtherLine that is just an OptArguments, which
    // would make these inputs valid. However,
    // - This parser is never used in that situation, because the
    //   SplitTorReplyLine parser enables OtherLine to be skipped.
    // - Even if these were valid, an OptArguments contains no semantic data,
    //   so there is no point in parsing it.
    CheckParseTorReplyMapping("ARGS", {});
    CheckParseTorReplyMapping("MORE ARGS", {});
    CheckParseTorReplyMapping("MORE  ARGS", {});
    CheckParseTorReplyMapping("EVEN more=ARGS", {});
    CheckParseTorReplyMapping("EVEN+more ARGS", {});
}

BOOST_AUTO_TEST_CASE(add_onion_maps_p2p_port_not_rpc)
{
    const CService target = LookupNumeric("127.0.0.1", 8762);
    BOOST_CHECK(target.IsValid());
    const std::string cmd = MakeAddOnionCommand("NEW:ED25519-V3", 8760, target);
    BOOST_CHECK_EQUAL(cmd, "ADD_ONION NEW:ED25519-V3 Port=8760,127.0.0.1:8762");
    BOOST_CHECK(cmd.find("8761") == std::string::npos);
    BOOST_CHECK(cmd.find("8334") == std::string::npos);
    BOOST_CHECK_EQUAL(cmd.find("Port="), cmd.rfind("Port="));
}

BOOST_AUTO_TEST_CASE(onion_target_ports_stay_off_p2p_and_rpc)
{
    const struct {
        const char* chain;
        uint16_t p2p;
        uint16_t rpc;
        uint16_t onion;
    } cases[] = {
        {"main", 8760, 8761, 8762},
        {"test", 18760, 18761, 18762},
        {"regtest", 28760, 28761, 28762},
        {"devtest", 48760, 48761, 48762},
        {"slowtest", 49760, 49761, 49762},
    };
    for (const auto& c : cases) {
        const std::unique_ptr<CBaseChainParams> params = CreateBaseChainParams(c.chain);
        BOOST_CHECK_EQUAL(params->RPCPort(), c.rpc);
        BOOST_CHECK_EQUAL(params->OnionServiceTargetPort(), c.onion);
        BOOST_CHECK(params->OnionServiceTargetPort() != c.p2p);
        BOOST_CHECK(params->OnionServiceTargetPort() != params->RPCPort());
        BOOST_CHECK(params->OnionServiceTargetPort() != 8334);
        BOOST_CHECK(params->OnionServiceTargetPort() != 18445);
    }
}

BOOST_AUTO_TEST_CASE(infer_tor_network_mode)
{
    TorModeInputs failed;
    failed.proxy_set = true;
    failed.listen = false;
    failed.listen_onion = false;
    BOOST_CHECK(InferTorNetworkMode(failed) == TorNetworkMode::OutboundOnly);

    TorModeInputs inbound;
    inbound.proxy_set = true;
    inbound.listen = true;
    inbound.listen_onion = true;
    BOOST_CHECK(InferTorNetworkMode(inbound) == TorNetworkMode::InboundOutbound);

    TorModeInputs dual;
    dual.separate_onion_proxy = true;
    dual.listen = true;
    dual.listen_onion = true;
    BOOST_CHECK(InferTorNetworkMode(dual) == TorNetworkMode::Dual);

    TorModeInputs only_tor;
    only_tor.proxy_set = true;
    only_tor.listen = true;
    only_tor.listen_onion = true;
    only_tor.onlynet_onion_only = true;
    only_tor.bind_is_onion_only = true;
    BOOST_CHECK(InferTorNetworkMode(only_tor) == TorNetworkMode::TorOnly);

    BOOST_CHECK(InferTorNetworkMode(TorModeInputs{}) == TorNetworkMode::Normal);

    TorSettingsPresence socks_only;
    socks_only.proxy_set = true;
    BOOST_CHECK(InferTorNetworkMode(ResolveTorModeInputs(socks_only)) == TorNetworkMode::OutboundOnly);

    TorSettingsPresence explicit_inbound;
    explicit_inbound.proxy_set = true;
    explicit_inbound.listen_present = true;
    explicit_inbound.listen_value = true;
    explicit_inbound.listen_onion_present = true;
    explicit_inbound.listen_onion_value = true;
    BOOST_CHECK(InferTorNetworkMode(ResolveTorModeInputs(explicit_inbound)) == TorNetworkMode::InboundOutbound);
}

BOOST_AUTO_TEST_CASE(onion_target_conflict)
{
    BOOST_CHECK(!OnionTargetConflictsWithNodePorts(8762, 8760, 8761, true));
    BOOST_CHECK(OnionTargetConflictsWithNodePorts(8761, 8760, 8761, true));
    BOOST_CHECK(OnionTargetConflictsWithNodePorts(8760, 8760, 8761, true));
    BOOST_CHECK(!OnionTargetConflictsWithNodePorts(8762, 8762, 8761, false));
    BOOST_CHECK(OnionTargetConflictsWithNodePorts(0, 8760, 8761, true));
    // Explicit -bind/-whitebind on the onion port collides even when -port
    // itself stays on the public P2P port and bind_on_any is false.
    BOOST_CHECK(OnionTargetConflictsWithNodePorts(8762, 8760, 8761, false, std::vector<uint16_t>{8762}));
    BOOST_CHECK(OnionTargetConflictsWithNodePorts(8762, 8760, 8761, false, std::vector<uint16_t>{8760, 8762}));
    BOOST_CHECK(!OnionTargetConflictsWithNodePorts(8762, 8760, 8761, false, std::vector<uint16_t>{8760}));
    for (const char* chain : {"main", "test", "regtest", "devtest", "slowtest"}) {
        const std::unique_ptr<CBaseChainParams> params = CreateBaseChainParams(chain);
        const uint16_t onion = params->OnionServiceTargetPort();
        const uint16_t rpc = params->RPCPort();
        const uint16_t listen = static_cast<uint16_t>(rpc - 1);
        BOOST_CHECK(!OnionTargetConflictsWithNodePorts(onion, listen, rpc, true));
    }
}

BOOST_AUTO_TEST_CASE(onion_key_file_is_restricted_to_the_current_user)
{
    const fs::path dir = fs::temp_directory_path() / "rigor-onion-acl-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "onion_v3_private_key";
    {
        std::ofstream out(file);
        out << "not-a-real-onion-key\n";
    }
    BOOST_CHECK(RestrictOnionKeyPermissions(file));
#ifndef WIN32
    struct stat st {};
    BOOST_CHECK_EQUAL(::stat(fs::PathToString(file).c_str(), &st), 0);
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600);
#else
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL dacl = nullptr;
    BOOST_CHECK_EQUAL(GetNamedSecurityInfoW(file.wstring().c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &descriptor), static_cast<DWORD>(ERROR_SUCCESS));
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    BOOST_CHECK(GetSecurityDescriptorControl(descriptor, &control, &revision));
    BOOST_CHECK((control & SE_DACL_PROTECTED) != 0);
    BOOST_CHECK(dacl != nullptr);
    if (dacl != nullptr) {
        ACL_SIZE_INFORMATION info{};
        BOOST_CHECK(GetAclInformation(dacl, &info, sizeof(info), AclSizeInformation));
        BOOST_CHECK_EQUAL(info.AceCount, 1u);
        for (DWORD i = 0; i < info.AceCount; ++i) {
            LPVOID ace = nullptr;
            BOOST_CHECK(GetAce(dacl, i, &ace));
            if (!ace) continue;
            const auto* header = static_cast<ACE_HEADER*>(ace);
            PSID sid = nullptr;
            if (header->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                sid = reinterpret_cast<PSID>(&reinterpret_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart);
            }
            LPSTR text = nullptr;
            if (sid != nullptr && ConvertSidToStringSidA(sid, &text)) {
                const std::string sid_text = text;
                LocalFree(text);
                BOOST_CHECK(sid_text != "S-1-1-0");
                BOOST_CHECK(sid_text != "S-1-5-11");
                BOOST_CHECK(sid_text != "S-1-5-32-545");
                BOOST_CHECK(sid_text != "S-1-5-32-544");
                BOOST_CHECK(sid_text.rfind("S-1-5-21-", 0) == 0);
            }
        }
    }
    LocalFree(descriptor);
#endif
    fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(tor_service_status_starts_inactive)
{
    const TorServiceStatus status = GetTorServiceStatus();
    BOOST_CHECK(!status.onion_service_active);
    BOOST_CHECK(status.onion_address.empty());
    BOOST_CHECK_EQUAL(status.bootstrap_percent, -1);
    BOOST_CHECK(!status.bootstrap_complete);
    BOOST_CHECK(!status.identity_restored);
    BOOST_CHECK(!status.identity_error);
    BOOST_CHECK(!status.socks_handshake_ok);
}

BOOST_AUTO_TEST_CASE(auth_method_prefers_safecookie_over_null)
{
    BOOST_CHECK(SelectTorAuthMethod(false, true, true, true) == TorAuthMethod::SafeCookie);
    BOOST_CHECK(SelectTorAuthMethod(false, false, true, true) == TorAuthMethod::SafeCookie);
    BOOST_CHECK(SelectTorAuthMethod(false, false, false, true) == TorAuthMethod::Null);
    BOOST_CHECK(SelectTorAuthMethod(true, true, true, true) == TorAuthMethod::HashedPassword);
    BOOST_CHECK(SelectTorAuthMethod(true, false, true, true) == TorAuthMethod::PasswordRequired);
    BOOST_CHECK(SelectTorAuthMethod(false, true, false, false) == TorAuthMethod::PasswordRequired);
    BOOST_CHECK(SelectTorAuthMethod(false, false, false, false) == TorAuthMethod::None);
    const std::string line = "ServiceID=abc PrivateKey=ED25519-V3:secretvalue Flags=None";
    const std::string redacted = RedactTorControlSecret(line);
    BOOST_CHECK(redacted.find("secretvalue") == std::string::npos);
    BOOST_CHECK(redacted.find("PrivateKey=[redacted]") != std::string::npos);
    BOOST_CHECK_EQUAL(RedactTorControlSecret("ServiceID=abc"), "ServiceID=abc");
}

static std::string ReadUntilQuiet(SOCKET client)
{
    std::string out;
    while (out.size() < 512) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(client, &fds);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = out.empty() ? 1500000 : 200000;
#ifdef WIN32
        const int ready = select(0, &fds, nullptr, nullptr, &tv);
#else
        const int ready = select(client + 1, &fds, nullptr, nullptr, &tv);
#endif
        if (ready <= 0) break;
        char buf[256];
        const int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, n);
    }
    return out;
}

static void CloseTestSocket(SOCKET fd)
{
#ifdef WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

static void ServeOnce(std::atomic<uint16_t>& port_out, std::string& received, const std::string& reply)
{
    const SOCKET listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOST_REQUIRE(listen_fd != INVALID_SOCKET);
    int yes = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);
    BOOST_REQUIRE_EQUAL(bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    BOOST_REQUIRE_EQUAL(listen(listen_fd, 1), 0);
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    BOOST_REQUIRE_EQUAL(getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len), 0);
    port_out.store(ntohs(bound.sin_port));
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(listen_fd, &fds);
    timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;
#ifdef WIN32
    const int ready = select(0, &fds, nullptr, nullptr, &tv);
#else
    const int ready = select(listen_fd + 1, &fds, nullptr, nullptr, &tv);
#endif
    if (ready > 0) {
        const SOCKET client = accept(listen_fd, nullptr, nullptr);
        if (client != INVALID_SOCKET) {
            received = ReadUntilQuiet(client);
            if (!reply.empty()) {
                send(client, reply.data(), reply.size(), 0);
                received += ReadUntilQuiet(client);
            }
            CloseTestSocket(client);
        }
    }
    CloseTestSocket(listen_fd);
}

BOOST_AUTO_TEST_CASE(onion_identity_is_not_replaced_when_the_existing_key_cannot_be_used)
{
    const fs::path dir = fs::temp_directory_path() / "rigor-onion-identity-guard";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path missing = dir / "missing_onion_v3_private_key";
    const OnionIdentityDecision created = LoadOnionIdentity(missing);
    BOOST_CHECK(created.action == OnionIdentityAction::CreateNew);
    BOOST_CHECK_EQUAL(OnionPrivateKeyForAdd(created), "NEW:ED25519-V3");
    BOOST_CHECK(!fs::exists(missing));

    const fs::path valid = dir / "onion_v3_private_key";
    const std::string key = "ED25519-V3:abcdefghijklmnopqrstuvwxyz0123456789ABCD=";
    {
        std::ofstream out(valid);
        out << key << "\n";
    }
    const OnionIdentityDecision reused = LoadOnionIdentity(valid);
    BOOST_CHECK(reused.action == OnionIdentityAction::UseExisting);
    BOOST_CHECK_EQUAL(reused.key, key);
    BOOST_CHECK_EQUAL(OnionPrivateKeyForAdd(reused), key);
    const OnionIdentityDecision reused_again = LoadOnionIdentity(valid);
    BOOST_CHECK_EQUAL(reused_again.key, reused.key);
    BOOST_CHECK_EQUAL(ReadBinaryFile(valid).second, key + "\n");

    const fs::path invalid = dir / "invalid_onion_v3_private_key";
    const std::string invalid_body = "not-an-ed25519-key\n";
    {
        std::ofstream out(invalid);
        out << invalid_body;
    }
    const OnionIdentityDecision refused = LoadOnionIdentity(invalid);
    BOOST_CHECK(refused.action == OnionIdentityAction::RefuseExisting);
    BOOST_CHECK(OnionPrivateKeyForAdd(refused).empty());
    BOOST_CHECK_EQUAL(ReadBinaryFile(invalid).second, invalid_body);

#ifndef WIN32
    if (::geteuid() != 0) {
        const fs::path unreadable = dir / "unreadable_onion_v3_private_key";
        {
            std::ofstream out(unreadable);
            out << key << "\n";
        }
        BOOST_CHECK_EQUAL(::chmod(fs::PathToString(unreadable).c_str(), 0), 0);
        const OnionIdentityDecision blocked = LoadOnionIdentity(unreadable);
        BOOST_CHECK(blocked.action == OnionIdentityAction::RefuseExisting);
        BOOST_CHECK(OnionPrivateKeyForAdd(blocked).empty());
        BOOST_CHECK_EQUAL(::chmod(fs::PathToString(unreadable).c_str(), 0600), 0);
        BOOST_CHECK_EQUAL(ReadBinaryFile(unreadable).second, key + "\n");
    }
#endif
    fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(bootstrap_progress_is_not_connectivity)
{
    BOOST_CHECK(!ParseBootstrapProgress("starting"));
    BOOST_CHECK_EQUAL(ParseBootstrapProgress("PROGRESS=0 TAG=starting").value_or(-1), 0);
    BOOST_CHECK_EQUAL(ParseBootstrapProgress("NOTICE PROGRESS=80").value_or(-1), 80);
    BOOST_CHECK_EQUAL(ParseBootstrapProgress("PROGRESS=100").value_or(-1), 100);
    BOOST_CHECK(!ParseBootstrapProgress("PROGRESS=101"));
    TorServiceStatus status;
    status.bootstrap_percent = 100;
    status.bootstrap_complete = true;
    BOOST_CHECK(!status.onion_service_active);
    BOOST_CHECK(!status.authenticated);
    BOOST_CHECK_EQUAL(status.onion_peers_in, 0);
    BOOST_CHECK(!status.network_active);
}

BOOST_AUTO_TEST_CASE(protocolinfo_requires_tor_evidence)
{
    std::string methods;
    const std::vector<std::string> tor_lines{
        "AUTH METHODS=SAFECOOKIE,NULL",
        "VERSION Tor=\"0.4.8.10\"",
    };
    BOOST_CHECK(TorProtocolInfoIsTor(250, tor_lines, methods));
    BOOST_CHECK_EQUAL(methods, "SAFECOOKIE,NULL");
    BOOST_CHECK(!TorProtocolInfoIsTor(250, {"AUTH METHODS=NULL"}, methods));
    BOOST_CHECK(!TorProtocolInfoIsTor(250, {"250 OK"}, methods));
    BOOST_CHECK(!TorProtocolInfoIsTor(510, tor_lines, methods));
}

BOOST_AUTO_TEST_CASE(probe_pairs_stay_on_loopback_and_are_capped)
{
    const std::vector<TorProbePair> remote = BuildTorProbePairs("192.168.1.2", 9050, "192.168.1.2", 9051);
    BOOST_CHECK_EQUAL(remote.size(), 2);
    BOOST_CHECK_EQUAL(remote[0].socks_port, 9050);
    BOOST_CHECK_EQUAL(remote[1].socks_port, 9150);
    const std::vector<TorProbePair> saved = BuildTorProbePairs("127.0.0.1", 9250, "127.0.0.1", 9251);
    BOOST_CHECK_EQUAL(saved.size(), 3);
    BOOST_CHECK_EQUAL(saved[0].socks_port, 9250);
    const std::vector<TorProbePair> duplicate = BuildTorProbePairs("127.0.0.1", 9050, "127.0.0.1", 9051);
    BOOST_CHECK_EQUAL(duplicate.size(), 2);

    TorProbePair refused;
    refused.socks_host = "8.8.8.8";
    refused.socks_port = 9050;
    refused.control_host = "8.8.8.8";
    refused.control_port = 9051;
    const std::vector<TorProbePair> skipped = ProbeExistingTor({refused}, 200);
    BOOST_CHECK_EQUAL(skipped.size(), 1);
    BOOST_CHECK(skipped[0].skipped_non_loopback);
    BOOST_CHECK(!skipped[0].socks_tcp);
    BOOST_CHECK(!skipped[0].control_tcp);
}

BOOST_AUTO_TEST_CASE(probe_requires_protocol_evidence_and_sends_only_protocolinfo)
{
    std::atomic<uint16_t> socks_port{0};
    std::atomic<uint16_t> control_port{0};
    std::string socks_bytes;
    std::string control_bytes;
    std::thread socks([&] { ServeOnce(socks_port, socks_bytes, std::string("\x05\x00", 2)); });
    std::thread control([&] {
        ServeOnce(control_port, control_bytes,
            "250-AUTH METHODS=SAFECOOKIE,NULL\r\n250-VERSION Tor=\"0.4.8.10\"\r\n250 OK\r\n");
    });
    while (socks_port.load() == 0 || control_port.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const std::string proxy_before = gArgs.GetArg("-proxy", "");
    TorProbePair requested;
    requested.socks_host = "127.0.0.1";
    requested.socks_port = socks_port.load();
    requested.control_host = "127.0.0.1";
    requested.control_port = control_port.load();
    const std::vector<TorProbePair> found = ProbeExistingTor({requested}, 2000);
    socks.join();
    control.join();
    BOOST_CHECK_EQUAL(found.size(), 1);
    BOOST_CHECK(found[0].socks_tcp);
    BOOST_CHECK(found[0].socks_greeting_ok);
    BOOST_CHECK(found[0].control_tcp);
    BOOST_CHECK(found[0].control_is_tor);
    BOOST_CHECK_EQUAL(socks_bytes, std::string("\x05\x01\x00", 3));
    BOOST_CHECK_EQUAL(control_bytes, "PROTOCOLINFO 1\r\n");
    BOOST_CHECK(control_bytes.find("AUTHENTICATE") == std::string::npos);
    BOOST_CHECK(control_bytes.find("ADD_ONION") == std::string::npos);
    BOOST_CHECK(control_bytes.find("DEL_ONION") == std::string::npos);
    BOOST_CHECK_EQUAL(gArgs.GetArg("-proxy", ""), proxy_before);
    BOOST_CHECK(GetTorServiceStatus().runtime_detected);
    BOOST_CHECK(!GetTorServiceStatus().authenticated);
    BOOST_CHECK(!GetTorServiceStatus().onion_service_active);
}

BOOST_AUTO_TEST_CASE(probe_rejects_open_port_that_is_not_tor)
{
    std::atomic<uint16_t> socks_port{0};
    std::atomic<uint16_t> control_port{0};
    std::string socks_bytes;
    std::string control_bytes;
    std::thread socks([&] { ServeOnce(socks_port, socks_bytes, "hello"); });
    std::thread control([&] { ServeOnce(control_port, control_bytes, "250 OK\r\n"); });
    while (socks_port.load() == 0 || control_port.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TorProbePair requested;
    requested.socks_host = "127.0.0.1";
    requested.socks_port = socks_port.load();
    requested.control_host = "127.0.0.1";
    requested.control_port = control_port.load();
    const std::vector<TorProbePair> found = ProbeExistingTor({requested}, 2000);
    socks.join();
    control.join();
    BOOST_CHECK(found[0].socks_tcp);
    BOOST_CHECK(!found[0].socks_greeting_ok);
    BOOST_CHECK(found[0].control_tcp);
    BOOST_CHECK(!found[0].control_is_tor);
    BOOST_CHECK(socks_bytes.find(std::string("\x05\x01\x00\x01", 4)) == std::string::npos);
}

BOOST_AUTO_TEST_CASE(tor_only_mode_does_not_infer_clearnet)
{
    TorModeInputs only_tor;
    only_tor.listen_onion = true;
    only_tor.onlynet_onion_only = true;
    only_tor.bind_is_onion_only = true;
    BOOST_CHECK(InferTorNetworkMode(only_tor) == TorNetworkMode::TorOnly);
    TorModeInputs clearnet;
    clearnet.listen = true;
    BOOST_CHECK(InferTorNetworkMode(clearnet) == TorNetworkMode::Normal);
}

BOOST_AUTO_TEST_SUITE_END()
