// Copyright (c) 2021-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <compat/compat.h>
#include <test/util/setup_common.h>
#include <util/sock.h>
#include <util/system.h>
#include <util/threadinterrupt.h>

#include <boost/test/unit_test.hpp>

#include <cassert>
#include <cstdint>
#include <thread>

using namespace std::chrono_literals;

BOOST_FIXTURE_TEST_SUITE(sock_tests, BasicTestingSetup)

static bool SocketIsClosed(const SOCKET& s)
{
    // Notice that if another thread is running and creates its own socket after `s` has been
    // closed, it may be assigned the same file descriptor number. In this case, our test will
    // wrongly pretend that the socket is not closed.
    int type;
    socklen_t len = sizeof(type);
    return getsockopt(s, SOL_SOCKET, SO_TYPE, (sockopt_arg_type)&type, &len) == SOCKET_ERROR;
}

static SOCKET CreateSocket()
{
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOST_REQUIRE(s != static_cast<SOCKET>(SOCKET_ERROR));
    return s;
}

BOOST_AUTO_TEST_CASE(constructor_and_destructor)
{
    const SOCKET s = CreateSocket();
    Sock* sock = new Sock(s);
    BOOST_CHECK_EQUAL(sock->Get(), s);
    BOOST_CHECK(!SocketIsClosed(s));
    delete sock;
    BOOST_CHECK(SocketIsClosed(s));
}

BOOST_AUTO_TEST_CASE(move_constructor)
{
    const SOCKET s = CreateSocket();
    Sock* sock1 = new Sock(s);
    Sock* sock2 = new Sock(std::move(*sock1));
    delete sock1;
    BOOST_CHECK(!SocketIsClosed(s));
    BOOST_CHECK_EQUAL(sock2->Get(), s);
    delete sock2;
    BOOST_CHECK(SocketIsClosed(s));
}

BOOST_AUTO_TEST_CASE(move_assignment)
{
    const SOCKET s = CreateSocket();
    Sock* sock1 = new Sock(s);
    Sock* sock2 = new Sock();
    *sock2 = std::move(*sock1);
    delete sock1;
    BOOST_CHECK(!SocketIsClosed(s));
    BOOST_CHECK_EQUAL(sock2->Get(), s);
    delete sock2;
    BOOST_CHECK(SocketIsClosed(s));
}

#ifndef WIN32 // Windows does not have socketpair(2).

static void CreateSocketPair(int s[2])
{
    BOOST_REQUIRE_EQUAL(socketpair(AF_UNIX, SOCK_STREAM, 0, s), 0);
}

static void SendAndRecvMessage(const Sock& sender, const Sock& receiver)
{
    const char* msg = "abcd";
    constexpr ssize_t msg_len = 4;
    char recv_buf[10];

    BOOST_CHECK_EQUAL(sender.Send(msg, msg_len, 0), msg_len);
    BOOST_CHECK_EQUAL(receiver.Recv(recv_buf, sizeof(recv_buf), 0), msg_len);
    BOOST_CHECK_EQUAL(strncmp(msg, recv_buf, msg_len), 0);
}

BOOST_AUTO_TEST_CASE(send_and_receive)
{
    int s[2];
    CreateSocketPair(s);

    Sock* sock0 = new Sock(s[0]);
    Sock* sock1 = new Sock(s[1]);

    SendAndRecvMessage(*sock0, *sock1);

    Sock* sock0moved = new Sock(std::move(*sock0));
    Sock* sock1moved = new Sock();
    *sock1moved = std::move(*sock1);

    delete sock0;
    delete sock1;

    SendAndRecvMessage(*sock1moved, *sock0moved);

    delete sock0moved;
    delete sock1moved;

    BOOST_CHECK(SocketIsClosed(s[0]));
    BOOST_CHECK(SocketIsClosed(s[1]));
}

BOOST_AUTO_TEST_CASE(wait)
{
    int s[2];
    CreateSocketPair(s);

    Sock sock0(s[0]);
    Sock sock1(s[1]);

    std::thread waiter([&sock0]() { (void)sock0.Wait(24h, Sock::RECV); });

    BOOST_REQUIRE_EQUAL(sock1.Send("a", 1, 0), 1);

    waiter.join();
}

BOOST_AUTO_TEST_CASE(recv_until_terminator_limit)
{
    constexpr auto timeout = 1min; // High enough so that it is never hit.
    CThreadInterrupt interrupt;
    int s[2];
    CreateSocketPair(s);

    Sock sock_send(s[0]);
    Sock sock_recv(s[1]);

    std::thread receiver([&sock_recv, &timeout, &interrupt]() {
        constexpr size_t max_data{10};
        bool threw_as_expected{false};
        // BOOST_CHECK_EXCEPTION() writes to some variables shared with the main thread which
        // creates a data race. So mimic it manually.
        try {
            (void)sock_recv.RecvUntilTerminator('\n', timeout, interrupt, max_data);
        } catch (const std::runtime_error& e) {
            threw_as_expected = HasReason("too many bytes without a terminator")(e);
        }
        assert(threw_as_expected);
    });

    BOOST_REQUIRE_NO_THROW(sock_send.SendComplete("1234567", timeout, interrupt));
    BOOST_REQUIRE_NO_THROW(sock_send.SendComplete("89a\n", timeout, interrupt));

    receiver.join();
}

#endif /* WIN32 */

static Sock MakeListenSock(uint16_t& port_out)
{
    const SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOST_REQUIRE(fd != static_cast<SOCKET>(SOCKET_ERROR));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    BOOST_REQUIRE_EQUAL(bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    BOOST_REQUIRE_EQUAL(listen(fd, 1), 0);
    socklen_t len = sizeof(addr);
    BOOST_REQUIRE_EQUAL(getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len), 0);
    port_out = ntohs(addr.sin_port);
    return Sock(fd);
}

static Sock ConnectLoopback(uint16_t port)
{
    const SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    BOOST_REQUIRE(fd != static_cast<SOCKET>(SOCKET_ERROR));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    BOOST_REQUIRE_EQUAL(connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    return Sock(fd);
}

static Sock AcceptOne(Sock& listener)
{
    const SOCKET fd = accept(listener.Get(), nullptr, nullptr);
    BOOST_REQUIRE(fd != static_cast<SOCKET>(SOCKET_ERROR));
    return Sock(fd);
}

// CVE-2026-117350: a paused read must not be woken by TCP urgent data, and a
// later requested read must still observe bytes that arrived during the pause.
BOOST_AUTO_TEST_CASE(paused_read_ignores_urgent_data)
{
    uint16_t port{0};
    Sock listener = MakeListenSock(port);
    Sock client = ConnectLoopback(port);
    Sock server = AcceptOne(listener);

    BOOST_CHECK_EQUAL(client.Send("N", 1, MSG_NOSIGNAL), 1);
    Sock::Event occurred{0};
    BOOST_CHECK(server.Wait(2s, Sock::RECV, &occurred));
    BOOST_CHECK(occurred & Sock::RECV);
    char buf[8];
    BOOST_CHECK_EQUAL(server.Recv(buf, sizeof(buf), MSG_DONTWAIT), 1);
    BOOST_CHECK_EQUAL(buf[0], 'N');

    BOOST_CHECK_EQUAL(client.Send("P", 1, MSG_NOSIGNAL), 1);
    occurred = Sock::RECV | Sock::ERR;
    BOOST_CHECK(server.Wait(200ms, 0, &occurred));
    BOOST_CHECK_EQUAL(occurred, 0);
    BOOST_CHECK(server.Wait(2s, Sock::RECV, &occurred));
    BOOST_CHECK(occurred & Sock::RECV);
    BOOST_CHECK_EQUAL(server.Recv(buf, sizeof(buf), MSG_DONTWAIT), 1);
    BOOST_CHECK_EQUAL(buf[0], 'P');

    const char urgent{'U'};
    const ssize_t urgent_sent = client.Send(&urgent, 1, MSG_OOB | MSG_NOSIGNAL);
    BOOST_REQUIRE_EQUAL(urgent_sent, 1);
    occurred = Sock::RECV | Sock::ERR;
    BOOST_CHECK(server.Wait(200ms, 0, &occurred));
    BOOST_CHECK_EQUAL(occurred, 0);
    occurred = 0;
    BOOST_CHECK(server.Wait(200ms, Sock::SEND, &occurred));
    BOOST_CHECK_EQUAL(occurred & (Sock::RECV | Sock::ERR), 0);

    BOOST_CHECK_EQUAL(client.Send("R", 1, MSG_NOSIGNAL), 1);
    occurred = 0;
    BOOST_CHECK(server.Wait(2s, Sock::RECV, &occurred));
    BOOST_CHECK(occurred & Sock::RECV);
    const ssize_t n = server.Recv(buf, sizeof(buf), MSG_DONTWAIT);
    BOOST_CHECK(n >= 1);
    BOOST_CHECK_EQUAL(buf[0], 'R');
}

BOOST_AUTO_TEST_SUITE_END()
