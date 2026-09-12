#include <winsock2.h>
#include <windows.h>
#include <winhttp.h>
#include "web_metadata.h"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace poetoolbox;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
void Check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
class WinsockRuntime final {
  public:
    WinsockRuntime() {
        WSADATA data{};
        const auto error = WSAStartup(MAKEWORD(2, 2), &data);
        Check(error == 0, "WSAStartup failed: " + std::to_string(error));
    }
    ~WinsockRuntime() { WSACleanup(); }
    WinsockRuntime(const WinsockRuntime &) = delete;
    WinsockRuntime &operator=(const WinsockRuntime &) = delete;
};
class Socket final {
  public:
    Socket() = default;
    explicit Socket(SOCKET value) : value_(value) {}
    ~Socket() { Reset(); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    void Reset(SOCKET value = INVALID_SOCKET) {
        if (value_ != INVALID_SOCKET)
            closesocket(value_);
        value_ = value;
    }
    [[nodiscard]] SOCKET Get() const { return value_; }
  private:
    SOCKET value_ = INVALID_SOCKET;
};
// Accept real TLS ClientHello bytes and send nothing. No certificate is installed,
// trusted, or bypassed. Nonblocking sockets and stop tokens bound all teardown paths.
class StalledTlsServer final {
  public:
    StalledTlsServer() : listener_(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        Check(listener_.Get() != INVALID_SOCKET, "Cannot create loopback socket");
        const BOOL exclusive = TRUE;
        Check(setsockopt(listener_.Get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                         reinterpret_cast<const char *>(&exclusive), sizeof(exclusive)) == 0,
              "Cannot reserve exclusive loopback socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        Check(bind(listener_.Get(), reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0,
              "Cannot bind 127.0.0.1");
        int addressSize = sizeof(address);
        Check(getsockname(listener_.Get(), reinterpret_cast<sockaddr *>(&address), &addressSize) == 0,
              "Cannot inspect loopback port");
        port_ = ntohs(address.sin_port);
        Check(listen(listener_.Get(), 8) == 0, "Cannot listen on loopback socket");
        u_long nonblocking = 1;
        Check(ioctlsocket(listener_.Get(), FIONBIO, &nonblocking) == 0, "Cannot make listener nonblocking");
        worker_ = std::jthread([this](std::stop_token stop) { Serve(stop); });
    }
    ~StalledTlsServer() {
        worker_.request_stop();
        if (worker_.joinable())
            worker_.join();
    }
    StalledTlsServer(const StalledTlsServer &) = delete;
    StalledTlsServer &operator=(const StalledTlsServer &) = delete;
    [[nodiscard]] std::string Url() const { return "https://127.0.0.1:" + std::to_string(port_) + "/stall"; }
    [[nodiscard]] unsigned Accepted() const { return accepted_.load(); }
    [[nodiscard]] unsigned Handshakes() const { return handshakes_.load(); }
    [[nodiscard]] unsigned Closed() const { return closed_.load(); }
    [[nodiscard]] int Error() const { return error_.load(); }
  private:
    void Serve(std::stop_token stop) {
        std::array<Socket, 16> connections;
        std::array<bool, 16> sawHandshake{};
        while (!stop.stop_requested()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener_.Get(), &readable);
            for (const auto &connection : connections)
                if (connection.Get() != INVALID_SOCKET)
                    FD_SET(connection.Get(), &readable);
            timeval wait{0, 10000};
            const auto ready = select(0, &readable, nullptr, nullptr, &wait);
            if (ready == SOCKET_ERROR) {
                error_.store(WSAGetLastError());
                return;
            }
            if (!ready)
                continue;
            if (FD_ISSET(listener_.Get(), &readable)) {
                const auto incoming = accept(listener_.Get(), nullptr, nullptr);
                if (incoming == INVALID_SOCKET) {
                    const auto error = WSAGetLastError();
                    if (error != WSAEWOULDBLOCK) {
                        error_.store(error);
                        return;
                    }
                } else {
                    size_t slot = 0;
                    while (slot < connections.size() && connections[slot].Get() != INVALID_SOCKET)
                        ++slot;
                    if (slot == connections.size()) {
                        closesocket(incoming);
                        error_.store(WSAENOBUFS);
                        return;
                    }
                    connections[slot].Reset(incoming);
                    sawHandshake[slot] = false;
                    u_long nonblocking = 1;
                    if (ioctlsocket(incoming, FIONBIO, &nonblocking) != 0) {
                        error_.store(WSAGetLastError());
                        return;
                    }
                    accepted_.fetch_add(1);
                }
            }
            for (size_t i = 0; i < connections.size(); ++i) {
                auto &connection = connections[i];
                if (connection.Get() == INVALID_SOCKET || !FD_ISSET(connection.Get(), &readable))
                    continue;
                std::array<unsigned char, 4096> bytes{};
                const int count = recv(connection.Get(), reinterpret_cast<char *>(bytes.data()),
                                        static_cast<int>(bytes.size()), 0);
                if (count > 0) {
                    if (!sawHandshake[i] && bytes.front() == 22) {
                        sawHandshake[i] = true;
                        handshakes_.fetch_add(1);
                    }
                } else if (count == 0 || WSAGetLastError() != WSAEWOULDBLOCK) {
                    connection.Reset();
                    closed_.fetch_add(1);
                }
            }
        }
    }
    Socket listener_;
    unsigned short port_ = 0;
    std::atomic<unsigned> accepted_{0}, handshakes_{0}, closed_{0};
    std::atomic<int> error_{0};
    std::jthread worker_;
};
std::string Describe(const Result<HttpResponse> &response) {
    if (response)
        return "unexpected HTTP status " + std::to_string(response->status);
    return response.error().message + " (native=" + std::to_string(response.error().nativeCode) + ")";
}
void DeadlineCase(WinHttpClient &client, StalledTlsServer &server) {
    const auto before = server.Handshakes();
    const auto started = Clock::now();
    auto response = client.Get({server.Url(), 1024, started + 250ms, {}});
    const auto elapsed = Clock::now() - started;
    Check(!response && response.error().nativeCode == ERROR_WINHTTP_TIMEOUT,
          "Short deadline did not time out: " + Describe(response));
    Check(elapsed >= 200ms && elapsed < 1s, "Short deadline did not bound a real pending TLS request");
    Check(server.Handshakes() > before, "Deadline test did not reach the local TLS listener");
    std::cout << "DeadlineMs=" << std::chrono::duration<double, std::milli>(elapsed).count() << '\n';
}
void CancellationCase(WinHttpClient &client, StalledTlsServer &server) {
    const auto before = server.Handshakes();
    std::stop_source requestStop;
    std::atomic<bool> issued{false};
    std::atomic<Clock::duration::rep> cancelledAt{0};
    // Wait for actual TLS bytes: a pre-cancelled short circuit cannot pass this test.
    std::jthread canceller([&](std::stop_token stop) {
        const auto deadline = Clock::now() + 1500ms;
        while (!stop.stop_requested() && Clock::now() < deadline) {
            if (server.Handshakes() > before) {
                cancelledAt.store(Clock::now().time_since_epoch().count());
                issued.store(true);
                requestStop.request_stop();
                return;
            }
            std::this_thread::sleep_for(5ms);
        }
    });
    const auto started = Clock::now();
    auto response = client.Get({server.Url(), 1024, started + 3s, requestStop.get_token()});
    const auto returned = Clock::now();
    canceller.request_stop();
    canceller.join();
    Check(issued.load(), "Cancellation test did not reach pending TLS: " + Describe(response));
    Check(!response && response.error().code == ErrorCode::Cancelled,
          "Pending WinHTTP cancellation failed: " + Describe(response));
    const auto latency = returned - Clock::time_point(Clock::duration(cancelledAt.load()));
    Check(latency >= 0ms && latency < 500ms, "Pending WinHTTP request did not cancel promptly");
    std::cout << "CancellationMs=" << std::chrono::duration<double, std::milli>(latency).count() << '\n';
}
} // namespace
int main() {
    try {
        WinsockRuntime winsock;
        StalledTlsServer server;
        WinHttpClient client;
        DeadlineCase(client, server);
        for (unsigned repeat = 0; repeat < 4; ++repeat)
            CancellationCase(client, server);
        DeadlineCase(client, server);
        const auto closeDeadline = Clock::now() + 1s;
        while (server.Closed() < server.Accepted() && Clock::now() < closeDeadline)
            std::this_thread::sleep_for(10ms);
        Check(server.Error() == 0, "Loopback service failed: " + std::to_string(server.Error()));
        Check(server.Accepted() == 6 && server.Handshakes() == 6 && server.Closed() == 6,
              "Cancelled/timed-out requests left loopback connections open");
        std::cout << "Real asynchronous WinHTTP deadline, in-flight cancellation, and connection cleanup passed; "
                     "127.0.0.1 only, TLS validation unchanged.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
