// Copyright (c) 2015-2022 The Bitcoin Core developers
// Copyright (c) 2017 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <torcontrol.h>
#include <tor_mode.h>

#include <chainparams.h>
#include <chainparamsbase.h>
#include <compat/compat.h>
#include <crypto/hmac_sha256.h>
#include <net.h>
#include <netaddress.h>
#include <netbase.h>
#include <sync.h>
#include <util/readwritefile.h>
#include <util/strencodings.h>
#include <util/syscall_sandbox.h>
#include <util/system.h>
#include <util/thread.h>
#include <util/time.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <system_error>
#include <thread>
#include <vector>

#ifdef WIN32
#include <windows.h>
#include <aclapi.h>
#endif

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/thread.h>
#include <event2/util.h>

/** Default control port */
const std::string DEFAULT_TOR_CONTROL = "127.0.0.1:9051";
/** Tor cookie size (from control-spec.txt) */
static const int TOR_COOKIE_SIZE = 32;
/** Size of client/server nonce for SAFECOOKIE */
static const int TOR_NONCE_SIZE = 32;
/** For computing serverHash in SAFECOOKIE */
static const std::string TOR_SAFE_SERVERKEY = "Tor safe cookie authentication server-to-controller hash";
/** For computing clientHash in SAFECOOKIE */
static const std::string TOR_SAFE_CLIENTKEY = "Tor safe cookie authentication controller-to-server hash";
/** Exponential backoff configuration - initial timeout in seconds */
static const float RECONNECT_TIMEOUT_START = 1.0;
/** Exponential backoff configuration - growth factor */
static const float RECONNECT_TIMEOUT_EXP = 1.5;
/** Maximum length for lines received on TorControlConnection.
 * tor-control-spec.txt mentions that there is explicitly no limit defined to line length,
 * this is belt-and-suspenders sanity limit to prevent memory exhaustion.
 */
static const int MAX_LINE_LENGTH = 100000;
static const uint16_t DEFAULT_TOR_SOCKS_PORT = 9050;

static Mutex g_tor_status_mutex;
static TorServiceStatus g_tor_status GUARDED_BY(g_tor_status_mutex);

static void UpdateTorServiceStatus(const std::function<void(TorServiceStatus&)>& fn)
{
    LOCK(g_tor_status_mutex);
    fn(g_tor_status);
}

TorServiceStatus GetTorServiceStatus()
{
    LOCK(g_tor_status_mutex);
    return g_tor_status;
}

void NoteTorPeerSnapshot(int onion_in, int onion_out, bool network_active)
{
    UpdateTorServiceStatus([&](TorServiceStatus& status) {
        status.onion_peers_in = onion_in;
        status.onion_peers_out = onion_out;
        status.network_active = network_active;
    });
}

bool IsLoopbackTorHost(const std::string& host)
{
    std::string normalized = host;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized == "127.0.0.1" || normalized == "::1" || normalized == "localhost";
}

static bool IsEd25519V3Key(const std::string& key)
{
    const std::string prefix = "ED25519-V3:";
    if (key.size() <= prefix.size() || key.compare(0, prefix.size(), prefix) != 0) return false;
    if (key.size() > 200) return false;
    for (size_t i = prefix.size(); i < key.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(key[i]);
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
        if (!ok) return false;
    }
    return true;
}

OnionIdentityDecision LoadOnionIdentity(const fs::path& path)
{
    OnionIdentityDecision decision;
    std::error_code exists_ec;
    const bool exists = std::filesystem::exists(path, exists_ec);
    if (exists_ec) {
        decision.action = OnionIdentityAction::RefuseExisting;
        decision.error = "Onion identity file could not be checked; a new identity was not created";
        return decision;
    }
    if (!exists) {
        decision.action = OnionIdentityAction::CreateNew;
        return decision;
    }
    const std::pair<bool, std::string> body = ReadBinaryFile(path);
    if (!body.first) {
        decision.action = OnionIdentityAction::RefuseExisting;
        decision.error = "Saved onion identity could not be read; a new identity was not created";
        return decision;
    }
    std::string key = body.second;
    while (!key.empty() && (key.back() == '\n' || key.back() == '\r')) key.pop_back();
    if (!IsEd25519V3Key(key)) {
        decision.action = OnionIdentityAction::RefuseExisting;
        decision.error = "Saved onion identity is not a usable ED25519-V3 key; it was not replaced";
        return decision;
    }
    decision.action = OnionIdentityAction::UseExisting;
    decision.key = key;
    return decision;
}

std::string OnionPrivateKeyForAdd(const OnionIdentityDecision& decision)
{
    if (decision.action == OnionIdentityAction::UseExisting) return decision.key;
    if (decision.action == OnionIdentityAction::CreateNew) return "NEW:ED25519-V3";
    return {};
}

std::pair<std::string, std::string> SplitTorReplyLine(const std::string& s);
std::map<std::string, std::string> ParseTorReplyMapping(const std::string& s);

std::optional<int> ParseBootstrapProgress(const std::string& line)
{
    const std::string key = "PROGRESS=";
    const size_t pos = line.find(key);
    if (pos == std::string::npos) return std::nullopt;
    size_t i = pos + key.size();
    if (i >= line.size() || !std::isdigit(static_cast<unsigned char>(line[i]))) return std::nullopt;
    int value = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        value = value * 10 + (line[i] - '0');
        if (value > 100) return std::nullopt;
        ++i;
    }
    return value;
}

bool TorProtocolInfoIsTor(int code, const std::vector<std::string>& lines, std::string& methods_out)
{
    methods_out.clear();
    if (code != 250) return false;
    bool saw_methods = false;
    bool saw_tor = false;
    for (const std::string& line : lines) {
        const std::pair<std::string, std::string> parts = SplitTorReplyLine(line);
        if (parts.first == "AUTH") {
            const std::map<std::string, std::string> mapping = ParseTorReplyMapping(parts.second);
            const auto methods = mapping.find("METHODS");
            if (methods != mapping.end() && !methods->second.empty()) {
                methods_out = methods->second;
                saw_methods = true;
            }
        } else if (parts.first == "VERSION") {
            const std::map<std::string, std::string> mapping = ParseTorReplyMapping(parts.second);
            const auto tor = mapping.find("Tor");
            if (tor != mapping.end() && !tor->second.empty()) saw_tor = true;
        }
    }
    return saw_methods && saw_tor;
}

static bool SameProbePair(const TorProbePair& a, const TorProbePair& b)
{
    return a.socks_host == b.socks_host && a.socks_port == b.socks_port &&
           a.control_host == b.control_host && a.control_port == b.control_port;
}

std::vector<TorProbePair> BuildTorProbePairs(const std::string& saved_socks_host, uint16_t saved_socks_port,
                                            const std::string& saved_control_host, uint16_t saved_control_port)
{
    std::vector<TorProbePair> pairs;
    auto add = [&](const std::string& socks_host, uint16_t socks_port, const std::string& control_host, uint16_t control_port) {
        if (pairs.size() >= 3) return;
        if (!IsLoopbackTorHost(socks_host) || !IsLoopbackTorHost(control_host)) return;
        if (socks_port < 1 || control_port < 1) return;
        TorProbePair pair;
        pair.socks_host = socks_host;
        pair.socks_port = socks_port;
        pair.control_host = control_host;
        pair.control_port = control_port;
        for (const TorProbePair& existing : pairs) {
            if (SameProbePair(existing, pair)) return;
        }
        pairs.push_back(pair);
    };
    add(saved_socks_host, saved_socks_port, saved_control_host, saved_control_port);
    add("127.0.0.1", 9050, "127.0.0.1", 9051);
    add("127.0.0.1", 9150, "127.0.0.1", 9151);
    return pairs;
}

TorNetworkMode RunningTorNetworkMode()
{
    TorModeInputs in;
    const std::string proxy = gArgs.GetArg("-proxy", "");
    const std::string onion = gArgs.GetArg("-onion", "");
    in.proxy_set = !proxy.empty() && proxy != "0";
    in.separate_onion_proxy = !onion.empty() && onion != "0";
    in.listen = gArgs.GetBoolArg("-listen", DEFAULT_LISTEN);
    in.listen_onion = gArgs.GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION);
    const std::vector<std::string> onlynets = gArgs.GetArgs("-onlynet");
    in.onlynet_onion_only = onlynets.size() == 1 && onlynets[0] == "onion";
    const std::vector<std::string> binds = gArgs.GetArgs("-bind");
    if (binds.size() == 1) {
        const std::string& bind = binds[0];
        const bool loopback = bind.find("127.0.0.1:") != std::string::npos || bind.find("[::1]:") != std::string::npos;
        in.bind_is_onion_only = loopback && bind.size() >= 6 && bind.compare(bind.size() - 6, 6, "=onion") == 0;
    }
    return InferTorNetworkMode(in);
}

static void EnsureLibeventThreads()
{
    static std::once_flag once;
    std::call_once(once, [] {
#ifdef WIN32
        evthread_use_windows_threads();
#else
        evthread_use_pthreads();
#endif
    });
}

static bool TcpConnectLoopback(const std::string& host, uint16_t port, int timeout_ms, std::unique_ptr<Sock>& out)
{
    if (!IsLoopbackTorHost(host) || port < 1) return false;
    CService address;
    if (!Lookup(host, address, port, false) || !address.IsValid()) return false;
    out = CreateSock(address);
    if (!out) return false;
    if (!ConnectSocketDirectly(address, *out, timeout_ms, true)) {
        out.reset();
        return false;
    }
    out->SetNonBlocking();
    return true;
}

static void ProbeConfiguredSocksBlocking()
{
    Proxy proxy;
    if (!GetProxy(NET_ONION, proxy) && !GetProxy(NET_IPV4, proxy)) return;
    if (!proxy.IsValid() || !IsLoopbackTorHost(proxy.proxy.ToStringAddr())) {
        UpdateTorServiceStatus([](TorServiceStatus& status) {
            status.socks_reachable = false;
            status.socks_handshake_ok = false;
        });
        return;
    }
    std::unique_ptr<Sock> sock;
    const bool tcp = TcpConnectLoopback(proxy.proxy.ToStringAddr(), proxy.proxy.GetPort(), 2000, sock);
    bool greeting_ok = false;
    if (tcp && sock) {
        const Socks5GreetingResult greeting = Socks5Greeting(*sock, nullptr, 2000);
        greeting_ok = greeting.ok && (greeting.method == 0x00 || greeting.method == 0x02);
    }
    UpdateTorServiceStatus([tcp, greeting_ok](TorServiceStatus& status) {
        status.socks_reachable = tcp;
        status.socks_handshake_ok = greeting_ok;
    });
}

void ProbeConfiguredSocksOnce()
{
    Proxy proxy;
    if (!GetProxy(NET_ONION, proxy) && !GetProxy(NET_IPV4, proxy)) return;
    if (!proxy.IsValid()) return;
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    std::thread([] { ProbeConfiguredSocksBlocking(); }).detach();
}

std::vector<TorProbePair> ProbeExistingTor(const std::vector<TorProbePair>& pairs, int timeout_ms)
{
    EnsureLibeventThreads();
    std::vector<TorProbePair> results;
    results.reserve(std::min<size_t>(pairs.size(), 3));
    for (const TorProbePair& requested : pairs) {
        if (results.size() >= 3) break;
        TorProbePair pair = requested;
        if (!IsLoopbackTorHost(pair.socks_host) || !IsLoopbackTorHost(pair.control_host)) {
            pair.skipped_non_loopback = true;
            pair.detail = "Refused a non-loopback Tor endpoint";
            results.push_back(pair);
            continue;
        }
        std::unique_ptr<Sock> socks;
        pair.socks_tcp = TcpConnectLoopback(pair.socks_host, pair.socks_port, timeout_ms, socks);
        if (pair.socks_tcp && socks) {
            const Socks5GreetingResult greeting = Socks5Greeting(*socks, nullptr, timeout_ms);
            pair.socks_greeting_ok = greeting.ok && (greeting.method == 0x00 || greeting.method == 0x02);
            pair.detail = pair.socks_greeting_ok ? "SOCKS5 greeting succeeded" : "TCP accepted, SOCKS5 greeting failed";
        } else {
            pair.detail = "SOCKS endpoint unreachable";
        }
        socks.reset();

        event_base* base = event_base_new();
        if (base) {
            struct ProbeState {
                event_base* base{nullptr};
                bool connected{false};
                bool finished{false};
                int code{0};
                std::vector<std::string> lines;
            } state;
            state.base = base;
            TorControlConnection conn(base);
            const std::string endpoint = strprintf("%s:%u", pair.control_host, pair.control_port);
            const bool began = conn.Connect(endpoint,
                [&](TorControlConnection& connection) {
                    state.connected = true;
                    connection.Command("PROTOCOLINFO 1", [&](TorControlConnection&, const TorControlReply& reply) {
                        state.code = reply.code;
                        state.lines = reply.lines;
                        state.finished = true;
                        event_base_loopbreak(state.base);
                    });
                },
                [&](TorControlConnection&) {
                    if (!state.finished) event_base_loopbreak(state.base);
                });
            if (began) {
                struct timeval tv = MillisToTimeval(timeout_ms);
                event_base_loopexit(base, &tv);
                event_base_dispatch(base);
            }
            conn.Disconnect();
            event_base_free(base);
            pair.control_tcp = state.connected;
            pair.control_is_tor = TorProtocolInfoIsTor(state.code, state.lines, pair.auth_methods);
        }
        if (pair.control_is_tor) {
            pair.detail += pair.detail.empty() ? "Tor control PROTOCOLINFO" : "; Tor control PROTOCOLINFO";
        } else if (pair.control_tcp) {
            pair.detail += "; port open, not a Tor control port";
        } else {
            pair.detail += "; control endpoint unreachable";
        }
        results.push_back(pair);
    }
    bool detected = false;
    for (const TorProbePair& pair : results) {
        if (pair.control_is_tor) detected = true;
    }
    if (detected) {
        UpdateTorServiceStatus([](TorServiceStatus& status) { status.runtime_detected = true; });
    }
    return results;
}

TorModeInputs ResolveTorModeInputs(const TorSettingsPresence& saved)
{
    TorModeInputs in;
    in.proxy_set = saved.proxy_set;
    in.separate_onion_proxy = saved.separate_onion_proxy;
    in.onlynet_onion_only = saved.onlynet_onion_only;
    in.bind_is_onion_only = saved.bind_is_onion_only;
    // -proxy SoftSets -listen=0, and -listen=0 SoftSets -listenonion=0,
    // when those flags were left at the default. A SOCKS checkbox alone
    // therefore does not create an onion service.
    if (saved.proxy_set && !saved.listen_present && !saved.listen_onion_present) {
        in.listen = false;
        in.listen_onion = false;
    } else {
        in.listen = saved.listen_present ? saved.listen_value : true;
        in.listen_onion = saved.listen_onion_present ? saved.listen_onion_value : true;
    }
    return in;
}

TorNetworkMode InferTorNetworkMode(const TorModeInputs& in)
{
    if (in.onlynet_onion_only && in.bind_is_onion_only && in.listen_onion) {
        return TorNetworkMode::TorOnly;
    }
    if (!in.proxy_set && in.separate_onion_proxy && in.listen_onion) {
        return TorNetworkMode::Dual;
    }
    if (in.proxy_set && in.listen_onion) {
        return TorNetworkMode::InboundOutbound;
    }
    if (in.proxy_set && !in.listen_onion) {
        return TorNetworkMode::OutboundOnly;
    }
    return TorNetworkMode::Normal;
}

std::string MakeAddOnionCommand(const std::string& private_key, uint16_t virtual_port, const CService& target)
{
    return strprintf("ADD_ONION %s Port=%u,%s", private_key, virtual_port, target.ToStringAddrPort());
}

TorAuthMethod SelectTorAuthMethod(bool password_configured, bool has_hashed_password, bool has_safecookie, bool has_null)
{
    if (password_configured) {
        return has_hashed_password ? TorAuthMethod::HashedPassword : TorAuthMethod::PasswordRequired;
    }
    if (has_safecookie) return TorAuthMethod::SafeCookie;
    if (has_null) return TorAuthMethod::Null;
    if (has_hashed_password) return TorAuthMethod::PasswordRequired;
    return TorAuthMethod::None;
}

std::string RedactTorControlSecret(const std::string& line)
{
    const std::string key = "PrivateKey=";
    const size_t pos = line.find(key);
    if (pos == std::string::npos) return line;
    const size_t value = pos + key.size();
    const size_t end = line.find(' ', value);
    std::string out = line;
    out.replace(value, end == std::string::npos ? std::string::npos : end - value, "[redacted]");
    return out;
}

bool RestrictOnionKeyPermissions(const fs::path& path)
{
#ifdef WIN32
    HANDLE process_token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token)) {
        LogPrintf("tor: OpenProcessToken failed for %s (%lu)\n", fs::PathToString(path), GetLastError());
        return false;
    }
    DWORD token_size = 0;
    GetTokenInformation(process_token, TokenUser, nullptr, 0, &token_size);
    std::vector<unsigned char> token_user(token_size);
    if (!GetTokenInformation(process_token, TokenUser, token_user.data(), token_size, &token_size)) {
        const DWORD error = GetLastError();
        CloseHandle(process_token);
        LogPrintf("tor: GetTokenInformation failed for %s (%lu)\n", fs::PathToString(path), error);
        return false;
    }
    CloseHandle(process_token);

    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = GENERIC_READ | GENERIC_WRITE | DELETE;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_USER;
    access.Trustee.ptstrName = static_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(token_user.data())->User.Sid);

    PACL acl = nullptr;
    const DWORD acl_result = SetEntriesInAclW(1, &access, nullptr, &acl);
    if (acl_result != ERROR_SUCCESS || acl == nullptr) {
        LogPrintf("tor: SetEntriesInAclW failed for %s (%lu)\n", fs::PathToString(path), acl_result);
        return false;
    }
    std::wstring wide_path = path.wstring();
    const DWORD set_result = SetNamedSecurityInfoW(
        wide_path.data(),
        SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr,
        nullptr,
        acl,
        nullptr);
    LocalFree(acl);
    if (set_result != ERROR_SUCCESS) {
        LogPrintf("tor: SetNamedSecurityInfoW failed for %s (%lu)\n", fs::PathToString(path), set_result);
        return false;
    }
    return true;
#else
    std::error_code ec;
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    if (ec) {
        LogPrintf("tor: Could not restrict permissions on %s: %s\n", fs::PathToString(path), ec.message());
        return false;
    }
    return true;
#endif
}

/****** Low-level TorControlConnection ********/

TorControlConnection::TorControlConnection(struct event_base* _base)
    : base(_base)
{
}

TorControlConnection::~TorControlConnection()
{
    if (b_conn)
        bufferevent_free(b_conn);
}

void TorControlConnection::readcb(struct bufferevent *bev, void *ctx)
{
    TorControlConnection *self = static_cast<TorControlConnection*>(ctx);
    struct evbuffer *input = bufferevent_get_input(bev);
    size_t n_read_out = 0;
    char *line;
    assert(input);
    //  If there is not a whole line to read, evbuffer_readln returns nullptr
    while((line = evbuffer_readln(input, &n_read_out, EVBUFFER_EOL_CRLF)) != nullptr)
    {
        std::string s(line, n_read_out);
        free(line);
        if (s.size() < 4) // Short line
            continue;
        // <status>(-|+| )<data><CRLF>
        self->message.code = LocaleIndependentAtoi<int>(s.substr(0,3));
        self->message.lines.push_back(s.substr(4));
        char ch = s[3]; // '-','+' or ' '
        if (ch == ' ') {
            // Final line, dispatch reply and clean up
            if (self->message.code >= 600) {
                // Dispatch async notifications to async handler
                // Synchronous and asynchronous messages are never interleaved
                self->async_handler(*self, self->message);
            } else {
                if (!self->reply_handlers.empty()) {
                    // Invoke reply handler with message
                    self->reply_handlers.front()(*self, self->message);
                    self->reply_handlers.pop_front();
                } else {
                    LogPrint(BCLog::TOR, "Received unexpected sync reply %i\n", self->message.code);
                }
            }
            self->message.Clear();
        }
    }
    //  Check for size of buffer - protect against memory exhaustion with very long lines
    //  Do this after evbuffer_readln to make sure all full lines have been
    //  removed from the buffer. Everything left is an incomplete line.
    if (evbuffer_get_length(input) > MAX_LINE_LENGTH) {
        LogPrintf("tor: Disconnecting because MAX_LINE_LENGTH exceeded\n");
        self->Disconnect();
    }
}

void TorControlConnection::eventcb(struct bufferevent *bev, short what, void *ctx)
{
    TorControlConnection *self = static_cast<TorControlConnection*>(ctx);
    if (what & BEV_EVENT_CONNECTED) {
        LogPrint(BCLog::TOR, "Successfully connected!\n");
        self->connected(*self);
    } else if (what & (BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
        if (what & BEV_EVENT_ERROR) {
            LogPrint(BCLog::TOR, "Error connecting to Tor control socket\n");
        } else {
            LogPrint(BCLog::TOR, "End of stream\n");
        }
        self->Disconnect();
        self->disconnected(*self);
    }
}

bool TorControlConnection::Connect(const std::string& tor_control_center, const ConnectionCB& _connected, const ConnectionCB& _disconnected)
{
    if (b_conn) {
        Disconnect();
    }

    CService control_service;
    if (!Lookup(tor_control_center, control_service, 9051, fNameLookup)) {
        LogPrintf("tor: Failed to look up control center %s\n", tor_control_center);
        return false;
    }

    struct sockaddr_storage control_address;
    socklen_t control_address_len = sizeof(control_address);
    if (!control_service.GetSockAddr(reinterpret_cast<struct sockaddr*>(&control_address), &control_address_len)) {
        LogPrintf("tor: Error parsing socket address %s\n", tor_control_center);
        return false;
    }

    // Create a new socket, set up callbacks and enable notification bits
    b_conn = bufferevent_socket_new(base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!b_conn) {
        return false;
    }
    bufferevent_setcb(b_conn, TorControlConnection::readcb, nullptr, TorControlConnection::eventcb, this);
    bufferevent_enable(b_conn, EV_READ|EV_WRITE);
    this->connected = _connected;
    this->disconnected = _disconnected;

    // Finally, connect to tor_control_center
    if (bufferevent_socket_connect(b_conn, reinterpret_cast<struct sockaddr*>(&control_address), control_address_len) < 0) {
        LogPrintf("tor: Error connecting to address %s\n", tor_control_center);
        return false;
    }
    return true;
}

void TorControlConnection::Disconnect()
{
    if (b_conn)
        bufferevent_free(b_conn);
    b_conn = nullptr;
}

bool TorControlConnection::Command(const std::string &cmd, const ReplyHandlerCB& reply_handler)
{
    if (!b_conn)
        return false;
    struct evbuffer *buf = bufferevent_get_output(b_conn);
    if (!buf)
        return false;
    evbuffer_add(buf, cmd.data(), cmd.size());
    evbuffer_add(buf, "\r\n", 2);
    reply_handlers.push_back(reply_handler);
    return true;
}

/****** General parsing utilities ********/

/* Split reply line in the form 'AUTH METHODS=...' into a type
 * 'AUTH' and arguments 'METHODS=...'.
 * Grammar is implicitly defined in https://spec.torproject.org/control-spec by
 * the server reply formats for PROTOCOLINFO (S3.21) and AUTHCHALLENGE (S3.24).
 */
std::pair<std::string,std::string> SplitTorReplyLine(const std::string &s)
{
    size_t ptr=0;
    std::string type;
    while (ptr < s.size() && s[ptr] != ' ') {
        type.push_back(s[ptr]);
        ++ptr;
    }
    if (ptr < s.size())
        ++ptr; // skip ' '
    return make_pair(type, s.substr(ptr));
}

/** Parse reply arguments in the form 'METHODS=COOKIE,SAFECOOKIE COOKIEFILE=".../control_auth_cookie"'.
 * Returns a map of keys to values, or an empty map if there was an error.
 * Grammar is implicitly defined in https://spec.torproject.org/control-spec by
 * the server reply formats for PROTOCOLINFO (S3.21), AUTHCHALLENGE (S3.24),
 * and ADD_ONION (S3.27). See also sections 2.1 and 2.3.
 */
std::map<std::string,std::string> ParseTorReplyMapping(const std::string &s)
{
    std::map<std::string,std::string> mapping;
    size_t ptr=0;
    while (ptr < s.size()) {
        std::string key, value;
        while (ptr < s.size() && s[ptr] != '=' && s[ptr] != ' ') {
            key.push_back(s[ptr]);
            ++ptr;
        }
        if (ptr == s.size()) // unexpected end of line
            return std::map<std::string,std::string>();
        if (s[ptr] == ' ') // The remaining string is an OptArguments
            break;
        ++ptr; // skip '='
        if (ptr < s.size() && s[ptr] == '"') { // Quoted string
            ++ptr; // skip opening '"'
            bool escape_next = false;
            while (ptr < s.size() && (escape_next || s[ptr] != '"')) {
                // Repeated backslashes must be interpreted as pairs
                escape_next = (s[ptr] == '\\' && !escape_next);
                value.push_back(s[ptr]);
                ++ptr;
            }
            if (ptr == s.size()) // unexpected end of line
                return std::map<std::string,std::string>();
            ++ptr; // skip closing '"'
            /**
             * Unescape value. Per https://spec.torproject.org/control-spec section 2.1.1:
             *
             *   For future-proofing, controller implementors MAY use the following
             *   rules to be compatible with buggy Tor implementations and with
             *   future ones that implement the spec as intended:
             *
             *     Read \n \t \r and \0 ... \377 as C escapes.
             *     Treat a backslash followed by any other character as that character.
             */
            std::string escaped_value;
            for (size_t i = 0; i < value.size(); ++i) {
                if (value[i] == '\\') {
                    // This will always be valid, because if the QuotedString
                    // ended in an odd number of backslashes, then the parser
                    // would already have returned above, due to a missing
                    // terminating double-quote.
                    ++i;
                    if (value[i] == 'n') {
                        escaped_value.push_back('\n');
                    } else if (value[i] == 't') {
                        escaped_value.push_back('\t');
                    } else if (value[i] == 'r') {
                        escaped_value.push_back('\r');
                    } else if ('0' <= value[i] && value[i] <= '7') {
                        size_t j;
                        // Octal escape sequences have a limit of three octal digits,
                        // but terminate at the first character that is not a valid
                        // octal digit if encountered sooner.
                        for (j = 1; j < 3 && (i+j) < value.size() && '0' <= value[i+j] && value[i+j] <= '7'; ++j) {}
                        // Tor restricts first digit to 0-3 for three-digit octals.
                        // A leading digit of 4-7 would therefore be interpreted as
                        // a two-digit octal.
                        if (j == 3 && value[i] > '3') {
                            j--;
                        }
                        const auto end{i + j};
                        uint8_t val{0};
                        while (i < end) {
                            val *= 8;
                            val += value[i++] - '0';
                        }
                        escaped_value.push_back(char(val));
                        // Account for automatic incrementing at loop end
                        --i;
                    } else {
                        escaped_value.push_back(value[i]);
                    }
                } else {
                    escaped_value.push_back(value[i]);
                }
            }
            value = escaped_value;
        } else { // Unquoted value. Note that values can contain '=' at will, just no spaces
            while (ptr < s.size() && s[ptr] != ' ') {
                value.push_back(s[ptr]);
                ++ptr;
            }
        }
        if (ptr < s.size() && s[ptr] == ' ')
            ++ptr; // skip ' ' after key=value
        mapping[key] = value;
    }
    return mapping;
}

TorController::TorController(struct event_base* _base, const std::string& tor_control_center, const CService& target):
    base(_base),
    m_tor_control_center(tor_control_center), conn(base), reconnect(true), reconnect_timeout(RECONNECT_TIMEOUT_START),
    m_target(target)
{
    reconnect_ev = event_new(base, -1, 0, reconnect_cb, this);
    if (!reconnect_ev)
        LogPrintf("tor: Failed to create event for reconnection: out of memory?\n");
    bootstrap_ev = event_new(_base, -1, 0, bootstrap_timer_cb, this);
    if (!bootstrap_ev)
        LogPrintf("tor: Failed to create event for bootstrap polling: out of memory?\n");
    // Start connection attempts immediately
    if (!conn.Connect(m_tor_control_center, std::bind(&TorController::connected_cb, this, std::placeholders::_1),
         std::bind(&TorController::disconnected_cb, this, std::placeholders::_1) )) {
        LogPrintf("tor: Initiating connection to Tor control port %s failed\n", m_tor_control_center);
    }
    // Read the cached onion identity. A present file that cannot be used
    // must not fall through to NEW: and must not be overwritten.
    m_identity = LoadOnionIdentity(GetPrivateKeyFile());
    if (m_identity.action == OnionIdentityAction::UseExisting) {
        LogPrint(BCLog::TOR, "Reading cached private key from %s\n", fs::PathToString(GetPrivateKeyFile()));
        private_key = m_identity.key;
    } else if (m_identity.action == OnionIdentityAction::RefuseExisting) {
        LogPrintf("tor: %s\n", m_identity.error);
        UpdateTorServiceStatus([&](TorServiceStatus& status) {
            status.identity_error = true;
            status.identity_restored = false;
            status.detail = m_identity.error;
        });
    }
}

TorController::~TorController()
{
    if (bootstrap_ev) {
        event_free(bootstrap_ev);
        bootstrap_ev = nullptr;
    }
    if (reconnect_ev) {
        event_free(reconnect_ev);
        reconnect_ev = nullptr;
    }
    if (service.IsValid()) {
        RemoveLocal(service);
    }
}

void TorController::get_socks_cb(TorControlConnection& _conn, const TorControlReply& reply)
{
    // NOTE: We can only get here if -onion is unset
    std::string socks_location;
    if (reply.code == 250) {
        for (const auto& line : reply.lines) {
            if (0 == line.compare(0, 20, "net/listeners/socks=")) {
                const std::string port_list_str = line.substr(20);
                std::vector<std::string> port_list = SplitString(port_list_str, ' ');

                for (auto& portstr : port_list) {
                    if (portstr.empty()) continue;
                    if ((portstr[0] == '"' || portstr[0] == '\'') && portstr.size() >= 2 && (*portstr.rbegin() == portstr[0])) {
                        portstr = portstr.substr(1, portstr.size() - 2);
                        if (portstr.empty()) continue;
                    }
                    socks_location = portstr;
                    if (0 == portstr.compare(0, 10, "127.0.0.1:")) {
                        // Prefer localhost - ignore other ports
                        break;
                    }
                }
            }
        }
        if (!socks_location.empty()) {
            LogPrint(BCLog::TOR, "Get SOCKS port command yielded %s\n", socks_location);
        } else {
            LogPrintf("tor: Get SOCKS port command returned nothing\n");
        }
    } else if (reply.code == 510) {  // 510 Unrecognized command
        LogPrintf("tor: Get SOCKS port command failed with unrecognized command (You probably should upgrade Tor)\n");
    } else {
        LogPrintf("tor: Get SOCKS port command failed; error code %d\n", reply.code);
    }

    CService resolved;
    Assume(!resolved.IsValid());
    if (!socks_location.empty()) {
        resolved = LookupNumeric(socks_location, DEFAULT_TOR_SOCKS_PORT);
    }
    if (!resolved.IsValid()) {
        // Fallback to old behaviour
        resolved = LookupNumeric("127.0.0.1", DEFAULT_TOR_SOCKS_PORT);
    }

    Assume(resolved.IsValid());
    LogPrint(BCLog::TOR, "Configuring onion proxy for %s\n", resolved.ToStringAddrPort());
    Proxy addrOnion = Proxy(resolved, true);
    SetProxy(NET_ONION, addrOnion);

    const auto onlynets = gArgs.GetArgs("-onlynet");

    const bool onion_allowed_by_onlynet{
        !gArgs.IsArgSet("-onlynet") ||
        std::any_of(onlynets.begin(), onlynets.end(), [](const auto& n) {
            return ParseNetwork(n) == NET_ONION;
        })};

    if (onion_allowed_by_onlynet) {
        // If NET_ONION is reachable, then the below is a noop.
        //
        // If NET_ONION is not reachable, then none of -proxy or -onion was given.
        // Since we are here, then -torcontrol and -torpassword were given.
        SetReachable(NET_ONION, true);
    }
}

void TorController::add_onion_cb(TorControlConnection& _conn, const TorControlReply& reply)
{
    if (reply.code == 250) {
        LogPrint(BCLog::TOR, "ADD_ONION successful\n");
        for (const std::string &s : reply.lines) {
            std::map<std::string,std::string> m = ParseTorReplyMapping(s);
            std::map<std::string,std::string>::iterator i;
            if ((i = m.find("ServiceID")) != m.end())
                service_id = i->second;
            if ((i = m.find("PrivateKey")) != m.end())
                private_key = i->second;
        }
        if (service_id.empty()) {
            LogPrintf("tor: Error parsing ADD_ONION parameters:\n");
            for (const std::string &s : reply.lines) {
                LogPrintf("    %s\n", SanitizeString(RedactTorControlSecret(s)));
            }
            UpdateTorServiceStatus([](TorServiceStatus& status) {
                status.onion_service_active = false;
                status.onion_address.clear();
                status.detail = "ADD_ONION succeeded but ServiceID was missing";
            });
            return;
        }
        service = LookupNumeric(std::string(service_id+".onion"), Params().GetDefaultPort());
        LogPrintfCategory(BCLog::TOR, "Got service ID %s, advertising service %s\n", service_id, service.ToStringAddrPort());
        if (WriteBinaryFile(GetPrivateKeyFile(), private_key)) {
            RestrictOnionKeyPermissions(GetPrivateKeyFile());
            LogPrint(BCLog::TOR, "Cached service private key to %s\n", fs::PathToString(GetPrivateKeyFile()));
        } else {
            LogPrintf("tor: Error writing service private key to %s\n", fs::PathToString(GetPrivateKeyFile()));
        }
        const bool advertised = AddLocal(service, LOCAL_MANUAL);
        const std::string onion_address = service_id + ".onion";
        const bool restored = m_identity.action == OnionIdentityAction::UseExisting;
        UpdateTorServiceStatus([&](TorServiceStatus& status) {
            status.authenticated = true;
            status.onion_service_active = true;
            status.identity_restored = restored;
            status.identity_error = false;
            status.onion_address = onion_address;
            status.detail = restored ?
                (advertised ? "Saved onion identity restored and registered for advertisement" :
                              "Saved onion identity restored; local advertisement was not accepted") :
                (advertised ? "Onion service created and registered for advertisement" :
                              "Onion service created; local advertisement was not accepted");
        });
        // ... onion requested - keep connection open
    } else if (reply.code == 510) { // 510 Unrecognized command
        LogPrintf("tor: Add onion failed with unrecognized command (You probably need to upgrade Tor)\n");
        UpdateTorServiceStatus([](TorServiceStatus& status) {
            status.onion_service_active = false;
            status.onion_address.clear();
            status.detail = "ADD_ONION unrecognized";
        });
    } else {
        LogPrintf("tor: Add onion failed; error code %d\n", reply.code);
        const int code = reply.code;
        UpdateTorServiceStatus([code](TorServiceStatus& status) {
            status.onion_service_active = false;
            status.onion_address.clear();
            status.detail = strprintf("ADD_ONION failed (%d)", code);
        });
    }
}

void TorController::RequestBootstrap()
{
    if (m_bootstrap_unsupported || m_bootstrap_query_inflight) return;
    if (!conn.Command("GETINFO status/bootstrap-phase", std::bind(&TorController::bootstrap_cb, this, std::placeholders::_1, std::placeholders::_2))) {
        return;
    }
    m_bootstrap_query_inflight = true;
}

void TorController::ArmBootstrapTimer()
{
    if (!bootstrap_ev) return;
    struct timeval tv = MillisToTimeval(2000);
    event_add(bootstrap_ev, &tv);
}

void TorController::bootstrap_timer_cb(evutil_socket_t fd, short what, void* arg)
{
    (void)fd;
    (void)what;
    TorController* self = static_cast<TorController*>(arg);
    self->RequestBootstrap();
}

void TorController::bootstrap_cb(TorControlConnection& _conn, const TorControlReply& reply)
{
    (void)_conn;
    m_bootstrap_query_inflight = false;
    if (reply.code == 510) {
        m_bootstrap_unsupported = true;
        UpdateTorServiceStatus([](TorServiceStatus& status) {
            status.bootstrap_percent = -1;
            status.bootstrap_complete = false;
        });
        return;
    }
    std::optional<int> progress;
    if (reply.code == 250) {
        for (const std::string& line : reply.lines) {
            progress = ParseBootstrapProgress(line);
            if (progress) break;
        }
    }
    if (!progress) {
        UpdateTorServiceStatus([](TorServiceStatus& status) {
            status.bootstrap_percent = -1;
            status.bootstrap_complete = false;
        });
        ArmBootstrapTimer();
        return;
    }
    const int percent = *progress;
    UpdateTorServiceStatus([percent](TorServiceStatus& status) {
        status.bootstrap_percent = percent;
        status.bootstrap_complete = percent == 100;
    });
    if (percent != 100) ArmBootstrapTimer();
}

void TorController::auth_cb(TorControlConnection& _conn, const TorControlReply& reply)
{
    if (reply.code == 250) {
        LogPrint(BCLog::TOR, "Authentication successful\n");
        const bool identity_blocked = m_identity.action == OnionIdentityAction::RefuseExisting;
        UpdateTorServiceStatus([&](TorServiceStatus& status) {
            status.control_connected = true;
            status.authenticated = true;
            status.onion_service_active = false;
            status.onion_address.clear();
            status.identity_error = identity_blocked;
            status.identity_restored = false;
            status.detail = identity_blocked ? m_identity.error : "Authenticated; onion service not created yet";
        });

        // Now that we know Tor is running setup the proxy for onion addresses
        // if -onion isn't set to something else.
        if (gArgs.GetArg("-onion", "") == "") {
            _conn.Command("GETINFO net/listeners/socks", std::bind(&TorController::get_socks_cb, this, std::placeholders::_1, std::placeholders::_2));
        }
        ProbeConfiguredSocksOnce();
        RequestBootstrap();

        if (identity_blocked) {
            LogPrintf("tor: %s\n", m_identity.error);
            return;
        }
        const std::string add_key = OnionPrivateKeyForAdd(m_identity);
        if (add_key.empty()) {
            UpdateTorServiceStatus([&](TorServiceStatus& status) {
                status.identity_error = true;
                status.detail = m_identity.error.empty() ? "Onion identity was not sent" : m_identity.error;
            });
            return;
        }
        private_key = add_key;
        // Request onion service, redirect port.
        // The virtual port is the default P2P port so a non-default -port does not decloak the node.
        _conn.Command(MakeAddOnionCommand(private_key, Params().GetDefaultPort(), m_target),
            std::bind(&TorController::add_onion_cb, this, std::placeholders::_1, std::placeholders::_2));
    } else {
        LogPrintf("tor: Authentication failed\n");
        UpdateTorServiceStatus([](TorServiceStatus& status) {
            status.authenticated = false;
            status.onion_service_active = false;
            status.onion_address.clear();
            status.detail = "Authentication failed";
        });
    }
}

/** Compute Tor SAFECOOKIE response.
 *
 *    ServerHash is computed as:
 *      HMAC-SHA256("Tor safe cookie authentication server-to-controller hash",
 *                  CookieString | ClientNonce | ServerNonce)
 *    (with the HMAC key as its first argument)
 *
 *    After a controller sends a successful AUTHCHALLENGE command, the
 *    next command sent on the connection must be an AUTHENTICATE command,
 *    and the only authentication string which that AUTHENTICATE command
 *    will accept is:
 *
 *      HMAC-SHA256("Tor safe cookie authentication controller-to-server hash",
 *                  CookieString | ClientNonce | ServerNonce)
 *
 */
static std::vector<uint8_t> ComputeResponse(const std::string &key, const std::vector<uint8_t> &cookie,  const std::vector<uint8_t> &clientNonce, const std::vector<uint8_t> &serverNonce)
{
    CHMAC_SHA256 computeHash((const uint8_t*)key.data(), key.size());
    std::vector<uint8_t> computedHash(CHMAC_SHA256::OUTPUT_SIZE, 0);
    computeHash.Write(cookie.data(), cookie.size());
    computeHash.Write(clientNonce.data(), clientNonce.size());
    computeHash.Write(serverNonce.data(), serverNonce.size());
    computeHash.Finalize(computedHash.data());
    return computedHash;
}

void TorController::authchallenge_cb(TorControlConnection& _conn, const TorControlReply& reply)
{
    if (reply.code == 250) {
        LogPrint(BCLog::TOR, "SAFECOOKIE authentication challenge successful\n");
        std::pair<std::string,std::string> l = SplitTorReplyLine(reply.lines[0]);
        if (l.first == "AUTHCHALLENGE") {
            std::map<std::string,std::string> m = ParseTorReplyMapping(l.second);
            if (m.empty()) {
                LogPrintf("tor: Error parsing AUTHCHALLENGE parameters: %s\n", SanitizeString(l.second));
                return;
            }
            std::vector<uint8_t> serverHash = ParseHex(m["SERVERHASH"]);
            std::vector<uint8_t> serverNonce = ParseHex(m["SERVERNONCE"]);
            LogPrint(BCLog::TOR, "AUTHCHALLENGE ServerHash %s ServerNonce %s\n", HexStr(serverHash), HexStr(serverNonce));
            if (serverNonce.size() != 32) {
                LogPrintf("tor: ServerNonce is not 32 bytes, as required by spec\n");
                return;
            }

            std::vector<uint8_t> computedServerHash = ComputeResponse(TOR_SAFE_SERVERKEY, cookie, clientNonce, serverNonce);
            if (computedServerHash != serverHash) {
                LogPrintf("tor: ServerHash %s does not match expected ServerHash %s\n", HexStr(serverHash), HexStr(computedServerHash));
                return;
            }

            std::vector<uint8_t> computedClientHash = ComputeResponse(TOR_SAFE_CLIENTKEY, cookie, clientNonce, serverNonce);
            _conn.Command("AUTHENTICATE " + HexStr(computedClientHash), std::bind(&TorController::auth_cb, this, std::placeholders::_1, std::placeholders::_2));
        } else {
            LogPrintf("tor: Invalid reply to AUTHCHALLENGE\n");
        }
    } else {
        LogPrintf("tor: SAFECOOKIE authentication challenge failed\n");
    }
}

void TorController::protocolinfo_cb(TorControlConnection& _conn, const TorControlReply& reply)
{
    if (reply.code == 250) {
        std::set<std::string> methods;
        std::string cookiefile;
        /*
         * 250-AUTH METHODS=COOKIE,SAFECOOKIE COOKIEFILE="/home/x/.tor/control_auth_cookie"
         * 250-AUTH METHODS=NULL
         * 250-AUTH METHODS=HASHEDPASSWORD
         */
        for (const std::string &s : reply.lines) {
            std::pair<std::string,std::string> l = SplitTorReplyLine(s);
            if (l.first == "AUTH") {
                std::map<std::string,std::string> m = ParseTorReplyMapping(l.second);
                std::map<std::string,std::string>::iterator i;
                if ((i = m.find("METHODS")) != m.end()) {
                    std::vector<std::string> m_vec = SplitString(i->second, ',');
                    methods = std::set<std::string>(m_vec.begin(), m_vec.end());
                }
                if ((i = m.find("COOKIEFILE")) != m.end())
                    cookiefile = i->second;
            } else if (l.first == "VERSION") {
                std::map<std::string,std::string> m = ParseTorReplyMapping(l.second);
                std::map<std::string,std::string>::iterator i;
                if ((i = m.find("Tor")) != m.end()) {
                    LogPrint(BCLog::TOR, "Connected to Tor version %s\n", i->second);
                }
            }
        }
        for (const std::string &s : methods) {
            LogPrint(BCLog::TOR, "Supported authentication method: %s\n", s);
        }
        if (!methods.empty()) {
            UpdateTorServiceStatus([](TorServiceStatus& status) { status.runtime_detected = true; });
        }
        // Password wins when the operator set one. Otherwise SAFECOOKIE wins
        // over NULL, so a control port that still advertises NULL cannot
        // skip cookie authentication. NULL remains only when it is the only
        // usable method.
        std::string torpassword = gArgs.GetArg("-torpassword", "");
        const TorAuthMethod auth_method = SelectTorAuthMethod(
            !torpassword.empty(),
            methods.count("HASHEDPASSWORD") != 0,
            methods.count("SAFECOOKIE") != 0,
            methods.count("NULL") != 0);
        if (auth_method == TorAuthMethod::HashedPassword) {
            LogPrint(BCLog::TOR, "Using HASHEDPASSWORD authentication\n");
            ReplaceAll(torpassword, "\"", "\\\"");
            _conn.Command("AUTHENTICATE \"" + torpassword + "\"", std::bind(&TorController::auth_cb, this, std::placeholders::_1, std::placeholders::_2));
        } else if (auth_method == TorAuthMethod::Null) {
            LogPrint(BCLog::TOR, "Using NULL authentication\n");
            _conn.Command("AUTHENTICATE", std::bind(&TorController::auth_cb, this, std::placeholders::_1, std::placeholders::_2));
        } else if (auth_method == TorAuthMethod::SafeCookie) {
            // Cookie: hexdump -e '32/1 "%02x""\n"'  ~/.tor/control_auth_cookie
            LogPrint(BCLog::TOR, "Using SAFECOOKIE authentication, reading cookie authentication from %s\n", cookiefile);
            std::pair<bool,std::string> status_cookie = ReadBinaryFile(fs::PathFromString(cookiefile), TOR_COOKIE_SIZE);
            if (status_cookie.first && status_cookie.second.size() == TOR_COOKIE_SIZE) {
                // _conn.Command("AUTHENTICATE " + HexStr(status_cookie.second), std::bind(&TorController::auth_cb, this, std::placeholders::_1, std::placeholders::_2));
                cookie = std::vector<uint8_t>(status_cookie.second.begin(), status_cookie.second.end());
                clientNonce = std::vector<uint8_t>(TOR_NONCE_SIZE, 0);
                GetRandBytes(clientNonce);
                _conn.Command("AUTHCHALLENGE SAFECOOKIE " + HexStr(clientNonce), std::bind(&TorController::authchallenge_cb, this, std::placeholders::_1, std::placeholders::_2));
            } else {
                if (status_cookie.first) {
                    LogPrintf("tor: Authentication cookie %s is not exactly %i bytes, as is required by the spec\n", cookiefile, TOR_COOKIE_SIZE);
                } else {
                    LogPrintf("tor: Authentication cookie %s could not be opened (check permissions)\n", cookiefile);
                }
            }
        } else if (auth_method == TorAuthMethod::PasswordRequired) {
            if (!torpassword.empty()) {
                LogPrintf("tor: Password provided with -torpassword, but HASHEDPASSWORD authentication is not available\n");
            } else {
                LogPrintf("tor: The only supported authentication mechanism left is password, but no password provided with -torpassword\n");
            }
        } else {
            LogPrintf("tor: No supported authentication method\n");
        }
    } else {
        LogPrintf("tor: Requesting protocol info failed\n");
    }
}

void TorController::connected_cb(TorControlConnection& _conn)
{
    reconnect_timeout = RECONNECT_TIMEOUT_START;
    UpdateTorServiceStatus([](TorServiceStatus& status) {
        status.control_reachable = true;
        status.control_connected = true;
        status.authenticated = false;
        status.onion_service_active = false;
        status.onion_address.clear();
        status.bootstrap_percent = -1;
        status.bootstrap_complete = false;
        status.detail = "Control connection open; authentication not finished";
    });
    // First send a PROTOCOLINFO command to figure out what authentication is expected
    if (!_conn.Command("PROTOCOLINFO 1", std::bind(&TorController::protocolinfo_cb, this, std::placeholders::_1, std::placeholders::_2)))
        LogPrintf("tor: Error sending initial protocolinfo command\n");
}

void TorController::disconnected_cb(TorControlConnection& _conn)
{
    // Stop advertising service when disconnected. Closing this control
    // connection also drops the non-detached onion service inside Tor.
    if (service.IsValid())
        RemoveLocal(service);
    service = CService();
    m_bootstrap_query_inflight = false;
    if (bootstrap_ev) event_del(bootstrap_ev);
    const bool identity_blocked = m_identity.action == OnionIdentityAction::RefuseExisting;
    const std::string identity_error = m_identity.error;
    UpdateTorServiceStatus([identity_blocked, identity_error](TorServiceStatus& status) {
        status.control_connected = false;
        status.authenticated = false;
        status.onion_service_active = false;
        status.onion_address.clear();
        status.identity_restored = false;
        status.identity_error = identity_blocked;
        status.bootstrap_percent = -1;
        status.bootstrap_complete = false;
        status.detail = identity_blocked ? identity_error :
            "Control connection closed; saved onion identity file was not changed";
    });
    if (!reconnect)
        return;

    LogPrint(BCLog::TOR, "Not connected to Tor control port %s, trying to reconnect\n", m_tor_control_center);

    // Single-shot timer for reconnect. Use exponential backoff.
    struct timeval time = MillisToTimeval(int64_t(reconnect_timeout * 1000.0));
    if (reconnect_ev)
        event_add(reconnect_ev, &time);
    reconnect_timeout *= RECONNECT_TIMEOUT_EXP;
}

void TorController::Reconnect()
{
    /* Try to reconnect and reestablish if we get booted - for example, Tor
     * may be restarting.
     */
    if (!conn.Connect(m_tor_control_center, std::bind(&TorController::connected_cb, this, std::placeholders::_1),
         std::bind(&TorController::disconnected_cb, this, std::placeholders::_1) )) {
        LogPrintf("tor: Re-initiating connection to Tor control port %s failed\n", m_tor_control_center);
    }
}

fs::path TorController::GetPrivateKeyFile()
{
    return gArgs.GetDataDirNet() / "onion_v3_private_key";
}

void TorController::reconnect_cb(evutil_socket_t fd, short what, void *arg)
{
    TorController *self = static_cast<TorController*>(arg);
    self->Reconnect();
}

/****** Thread ********/
static struct event_base *gBase;
static std::thread torControlThread;

static void TorControlThread(CService onion_service_target)
{
    SetSyscallSandboxPolicy(SyscallSandboxPolicy::TOR_CONTROL);
    TorController ctrl(gBase, gArgs.GetArg("-torcontrol", DEFAULT_TOR_CONTROL), onion_service_target);

    event_base_dispatch(gBase);
}

void StartTorControl(CService onion_service_target)
{
    assert(!gBase);
#ifdef WIN32
    evthread_use_windows_threads();
#else
    evthread_use_pthreads();
#endif
    gBase = event_base_new();
    if (!gBase) {
        LogPrintf("tor: Unable to create event_base\n");
        return;
    }

    torControlThread = std::thread(&util::TraceThread, "torcontrol", [onion_service_target] {
        TorControlThread(onion_service_target);
    });
}

void InterruptTorControl()
{
    if (gBase) {
        LogPrintf("tor: Thread interrupt\n");
        event_base_once(gBase, -1, EV_TIMEOUT, [](evutil_socket_t, short, void*) {
            event_base_loopbreak(gBase);
        }, nullptr, nullptr);
    }
}

void StopTorControl()
{
    if (gBase) {
        torControlThread.join();
        event_base_free(gBase);
        gBase = nullptr;
    }
}

CService DefaultOnionServiceTarget()
{
    struct in_addr onion_service_target;
    onion_service_target.s_addr = htonl(INADDR_LOOPBACK);
    return {onion_service_target, BaseParams().OnionServiceTargetPort()};
}

bool OnionTargetConflictsWithNodePorts(uint16_t onion_port, uint16_t listen_port, uint16_t rpc_port, bool bind_on_any, const std::vector<uint16_t>& explicit_bind_ports)
{
    if (onion_port == 0 || onion_port == rpc_port) return true;
    // 0.0.0.0:listen_port already accepts 127.0.0.1:listen_port, so a second
    // listen socket on that same port cannot be bound.
    if (bind_on_any && onion_port == listen_port) return true;
    // An explicit -bind/-whitebind on the onion port is a separate listener
    // from the =onion socket. Same port on 0.0.0.0, or a second bind of
    // 127.0.0.1:<onion>, fails or exposes the target. A bind on another port
    // does not.
    for (const uint16_t bound_port : explicit_bind_ports) {
        if (bound_port == onion_port) return true;
    }
    return false;
}
