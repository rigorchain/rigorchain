// Copyright (c) 2015-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Functionality for communicating with Tor.
 */
#ifndef BITCOIN_TORCONTROL_H
#define BITCOIN_TORCONTROL_H

#include <netaddress.h>
#include <tor_mode.h>
#include <util/fs.h>

#include <boost/signals2/signal.hpp>

#include <event2/bufferevent.h>
#include <event2/event.h>

#include <cstdlib>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class CService;

extern const std::string DEFAULT_TOR_CONTROL;
static const bool DEFAULT_LISTEN_ONION = true;

void StartTorControl(CService onion_service_target);
void InterruptTorControl();
void StopTorControl();

CService DefaultOnionServiceTarget();

/** True when the onion loopback target would collide with the public P2P
 *  listener or the RPC port. A separate loopback listener is required; it
 *  must not reuse either of those ports.
 */
bool OnionTargetConflictsWithNodePorts(uint16_t onion_port, uint16_t listen_port, uint16_t rpc_port, bool bind_on_any, const std::vector<uint16_t>& explicit_bind_ports = {});

/** Leave a sensitive file readable and writable by the current user only.
 *  POSIX: mode 0600. Windows: a protected DACL for the process user, with
 *  inheritance removed. Returns false when the platform call fails.
 */
bool RestrictOnionKeyPermissions(const fs::path& path);

/** ADD_ONION command. virtual_port is the advertised P2P port. target is the
 *  loopback listener. The command has one Port mapping and must not name the RPC port.
 */
std::string MakeAddOnionCommand(const std::string& private_key, uint16_t virtual_port, const CService& target);

/** Which control authentication to attempt. NULL is used only when no
 *  stronger advertised method can be used. A configured password never falls
 *  through to NULL or SAFECOOKIE.
 */
enum class TorAuthMethod {
    HashedPassword,
    SafeCookie,
    Null,
    PasswordRequired,
    None,
};

TorAuthMethod SelectTorAuthMethod(bool password_configured, bool has_hashed_password, bool has_safecookie, bool has_null);

/** Remove a PrivateKey value before a control reply is written to the log. */
std::string RedactTorControlSecret(const std::string& line);

/** Runtime result of the control connection. onion_service_active is set only
 *  after ADD_ONION returns 250 and a service id is parsed.
 */
struct TorServiceStatus {
    bool control_connected{false};
    bool authenticated{false};
    bool onion_service_active{false};
    std::string onion_address;
    std::string detail;
    /** A probe received a Tor PROTOCOLINFO. This is not authentication. */
    bool runtime_detected{false};
    bool socks_reachable{false};
    /** True only after Socks5Greeting succeeds against the configured proxy. */
    bool socks_handshake_ok{false};
    bool control_reachable{false};
    /** -1 means the controller has not parsed a bootstrap percent. Never treat -1 as zero or as complete. */
    int bootstrap_percent{-1};
    bool bootstrap_complete{false};
    /** True only after ADD_ONION reused a key loaded from onion_v3_private_key. */
    bool identity_restored{false};
    /** The key file exists and could not be used. A new identity was not created. */
    bool identity_error{false};
    int onion_peers_in{0};
    int onion_peers_out{0};
    bool network_active{false};
};

TorServiceStatus GetTorServiceStatus();

/** Record peer counts and the P2P on/off flag. Does not change Tor controller fields. */
void NoteTorPeerSnapshot(int onion_in, int onion_out, bool network_active);

/** Hosts the Tor-mode writer and the probe are allowed to use. */
bool IsLoopbackTorHost(const std::string& host);

enum class OnionIdentityAction {
    CreateNew,
    UseExisting,
    RefuseExisting,
};

struct OnionIdentityDecision {
    OnionIdentityAction action{OnionIdentityAction::CreateNew};
    std::string key;
    std::string error;
};

/** Read path and decide whether ADD_ONION may mint a new key. Does not write the file. */
OnionIdentityDecision LoadOnionIdentity(const fs::path& path);

/** Key argument for ADD_ONION. Empty when the existing file must be left untouched. */
std::string OnionPrivateKeyForAdd(const OnionIdentityDecision& decision);

/** Parse PROGRESS= from a bootstrap GETINFO line. Empty when the line has no usable percent. */
std::optional<int> ParseBootstrapProgress(const std::string& line);

/** True when a PROTOCOLINFO body is Tor. Fills methods with the AUTH METHODS value. */
bool TorProtocolInfoIsTor(int code, const std::vector<std::string>& lines, std::string& methods_out);

struct TorProbePair {
    std::string socks_host;
    uint16_t socks_port{0};
    std::string control_host;
    uint16_t control_port{0};
    bool skipped_non_loopback{false};
    bool socks_tcp{false};
    bool socks_greeting_ok{false};
    bool control_tcp{false};
    bool control_is_tor{false};
    std::string auth_methods;
    std::string detail;
};

/**
 * Saved loopback pair, then 127.0.0.1:9050/9051, then 127.0.0.1:9150/9151.
 * At most three pairs. A non-loopback saved pair is omitted.
 */
std::vector<TorProbePair> BuildTorProbePairs(const std::string& saved_socks_host, uint16_t saved_socks_port,
                                            const std::string& saved_control_host, uint16_t saved_control_port);

/**
 * Probe the given pairs. Sends a SOCKS5 greeting and PROTOCOLINFO only.
 * Does not authenticate, does not send ADD_ONION, and does not write settings.
 * timeout_ms bounds each TCP connect and each read.
 */
std::vector<TorProbePair> ProbeExistingTor(const std::vector<TorProbePair>& pairs, int timeout_ms);

/** Effective mode of the running process, from gArgs rather than settings.json. */
TorNetworkMode RunningTorNetworkMode();

/** One SOCKS greeting against the proxy the running node is using. Not a CONNECT. */
void ProbeConfiguredSocksOnce();

/** Reply from Tor, can be single or multi-line */
class TorControlReply
{
public:
    TorControlReply() { Clear(); }

    int code;
    std::vector<std::string> lines;

    void Clear()
    {
        code = 0;
        lines.clear();
    }
};

/** Low-level handling for Tor control connection.
 * Speaks the SMTP-like protocol as defined in torspec/control-spec.txt
 */
class TorControlConnection
{
public:
    typedef std::function<void(TorControlConnection&)> ConnectionCB;
    typedef std::function<void(TorControlConnection &,const TorControlReply &)> ReplyHandlerCB;

    /** Create a new TorControlConnection.
     */
    explicit TorControlConnection(struct event_base *base);
    ~TorControlConnection();

    /**
     * Connect to a Tor control port.
     * tor_control_center is address of the form host:port.
     * connected is the handler that is called when connection is successfully established.
     * disconnected is a handler that is called when the connection is broken.
     * Return true on success.
     */
    bool Connect(const std::string& tor_control_center, const ConnectionCB& connected, const ConnectionCB& disconnected);

    /**
     * Disconnect from Tor control port.
     */
    void Disconnect();

    /** Send a command, register a handler for the reply.
     * A trailing CRLF is automatically added.
     * Return true on success.
     */
    bool Command(const std::string &cmd, const ReplyHandlerCB& reply_handler);

    /** Response handlers for async replies */
    boost::signals2::signal<void(TorControlConnection &,const TorControlReply &)> async_handler;
private:
    /** Callback when ready for use */
    std::function<void(TorControlConnection&)> connected;
    /** Callback when connection lost */
    std::function<void(TorControlConnection&)> disconnected;
    /** Libevent event base */
    struct event_base *base;
    /** Connection to control socket */
    struct bufferevent* b_conn{nullptr};
    /** Message being received */
    TorControlReply message;
    /** Response handlers */
    std::deque<ReplyHandlerCB> reply_handlers;

    /** Libevent handlers: internal */
    static void readcb(struct bufferevent *bev, void *ctx);
    static void eventcb(struct bufferevent *bev, short what, void *ctx);
};

/****** Bitcoin specific TorController implementation ********/

/** Controller that connects to Tor control socket, authenticate, then create
 * and maintain an ephemeral onion service.
 */
class TorController
{
public:
    TorController(struct event_base* base, const std::string& tor_control_center, const CService& target);
    TorController() : conn{nullptr} {
        // Used for testing only.
    }
    ~TorController();

    /** Get name of file to store private key in */
    fs::path GetPrivateKeyFile();

    /** Reconnect, after getting disconnected */
    void Reconnect();
private:
    struct event_base* base;
    const std::string m_tor_control_center;
    TorControlConnection conn;
    std::string private_key;
    std::string service_id;
    bool reconnect;
    struct event *reconnect_ev = nullptr;
    struct event *bootstrap_ev = nullptr;
    float reconnect_timeout;
    CService service;
    const CService m_target;
    OnionIdentityDecision m_identity;
    bool m_bootstrap_query_inflight{false};
    bool m_bootstrap_unsupported{false};
    /** Cookie for SAFECOOKIE auth */
    std::vector<uint8_t> cookie;
    /** ClientNonce for SAFECOOKIE auth */
    std::vector<uint8_t> clientNonce;

public:
    /** Callback for GETINFO net/listeners/socks result */
    void get_socks_cb(TorControlConnection& conn, const TorControlReply& reply);
    /** Callback for GETINFO status/bootstrap-phase. Does not change onion-service state. */
    void bootstrap_cb(TorControlConnection& conn, const TorControlReply& reply);
    void RequestBootstrap();
    void ArmBootstrapTimer();
    static void bootstrap_timer_cb(evutil_socket_t fd, short what, void *arg);
    /** Callback for ADD_ONION result */
    void add_onion_cb(TorControlConnection& conn, const TorControlReply& reply);
    /** Callback for AUTHENTICATE result */
    void auth_cb(TorControlConnection& conn, const TorControlReply& reply);
    /** Callback for AUTHCHALLENGE result */
    void authchallenge_cb(TorControlConnection& conn, const TorControlReply& reply);
    /** Callback for PROTOCOLINFO result */
    void protocolinfo_cb(TorControlConnection& conn, const TorControlReply& reply);
    /** Callback after successful connection */
    void connected_cb(TorControlConnection& conn);
    /** Callback after connection lost or failed connection attempt */
    void disconnected_cb(TorControlConnection& conn);

    /** Callback for reconnect timer */
    static void reconnect_cb(evutil_socket_t fd, short what, void *arg);
};

#endif // BITCOIN_TORCONTROL_H
