// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//

#include <boost/test/unit_test.hpp>

#include <chainparamsbase.h>
#include <netbase.h>
#include <tor_mode.h>
#include <torcontrol.h>

#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32
#include <sys/stat.h>
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

BOOST_AUTO_TEST_SUITE_END()
