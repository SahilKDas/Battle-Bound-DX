#include <winsock2.h>
#include <ws2tcpip.h>
#include "multiplayer.hpp"
#include <array>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace bb {
namespace {
constexpr std::size_t maxFrame = 8192;
constexpr std::size_t maxAttacks = 16;

void sendAll(SOCKET socket, const char* bytes, int size) {
    while (size > 0) {
        const int sent = send(socket, bytes, size, 0);
        if (sent <= 0) throw std::runtime_error("Multiplayer send failed");
        size -= sent;
        bytes += sent;
    }
}
void receiveAll(SOCKET socket, char* bytes, int size) {
    while (size > 0) {
        const int read = recv(socket, bytes, size, 0);
        if (read <= 0) throw std::runtime_error("Multiplayer peer disconnected or timed out");
        size -= read;
        bytes += read;
    }
}
bool bounded(double value, double limit) { return std::isfinite(value) && std::abs(value) <= limit; }
}

struct PeerConnection::Socket {
    SOCKET value{INVALID_SOCKET};
    bool ready{};
    std::uint32_t epoch{};
    ~Socket() {
        if (value != INVALID_SOCKET) closesocket(value);
        if (ready) WSACleanup();
    }
};

PeerConnection::PeerConnection(unsigned port, const std::string& host)
    : connection(std::make_unique<Socket>()) {
    if (!port || port > 65535) throw std::runtime_error("Invalid multiplayer port");
    WSADATA startup{};
    if (WSAStartup(MAKEWORD(2, 2), &startup)) throw std::runtime_error("Winsock initialization failed");
    connection->ready = true;
    auto& socket = connection->value;
    socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) throw std::runtime_error("Cannot create multiplayer socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (host.empty()) {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) || listen(socket, 1))
            throw std::runtime_error("Cannot listen on multiplayer port");
        fd_set pending;
        FD_ZERO(&pending);
        FD_SET(socket, &pending);
        timeval timeout{30, 0};
        if (select(0, &pending, nullptr, nullptr, &timeout) != 1)
            throw std::runtime_error("No multiplayer peer connected within 30 seconds");
        const auto accepted = accept(socket, nullptr, nullptr);
        if (accepted == INVALID_SOCKET) throw std::runtime_error("Cannot accept multiplayer peer");
        closesocket(socket);
        socket = accepted;
    } else {
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("Join requires a numeric IPv4 address");
        if (connect(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)))
            throw std::runtime_error("Cannot connect to multiplayer host");
    }
    DWORD timeout = 5000;
    int yes = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ||
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout)) ||
        setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&yes), sizeof(yes)))
        throw std::runtime_error("Cannot configure multiplayer socket");
    std::array<unsigned long, 2> hello{};
    if (host.empty()) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        connection->epoch = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
        hello = {htonl(0x42424431), htonl(connection->epoch)};
        sendAll(socket, reinterpret_cast<const char*>(hello.data()), sizeof(hello));
    } else {
        receiveAll(socket, reinterpret_cast<char*>(hello.data()), sizeof(hello));
        if (ntohl(hello[0]) != 0x42424431) throw std::runtime_error("Incompatible multiplayer protocol");
        connection->epoch = ntohl(hello[1]);
    }
}
PeerConnection::~PeerConnection() = default;
std::int64_t PeerConnection::epoch() const { return connection->epoch; }

NetworkFrame PeerConnection::exchange(const NetworkFrame& local) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "BBDX1 " << local.position.x << ' ' << local.position.y << ' '
        << local.direction << ' ' << local.size << ' ' << local.costume << ' ' << local.visible
        << ' ' << std::quoted(local.name) << ' ' << local.attacks.size();
    for (const auto& attack : local.attacks) {
        out << ' ' << static_cast<unsigned>(attack.kind) << ' ' << attack.item << ' '
            << attack.origin.x << ' ' << attack.origin.y << ' ' << attack.velocity.x << ' '
            << attack.velocity.y << ' ' << attack.angle << ' ' << attack.charge;
    }
    const auto payload = out.str();
    if (payload.size() > maxFrame || local.attacks.size() > maxAttacks)
        throw std::runtime_error("Outgoing multiplayer frame exceeds limits");
    auto length = htonl(static_cast<unsigned long>(payload.size()));
    sendAll(connection->value, reinterpret_cast<const char*>(&length), sizeof(length));
    sendAll(connection->value, payload.data(), static_cast<int>(payload.size()));
    receiveAll(connection->value, reinterpret_cast<char*>(&length), sizeof(length));
    length = ntohl(length);
    if (!length || length > maxFrame) throw std::runtime_error("Invalid multiplayer frame size");
    std::string bytes(length, '\0');
    receiveAll(connection->value, bytes.data(), static_cast<int>(length));

    std::istringstream in(bytes);
    in.imbue(std::locale::classic());
    NetworkFrame remote;
    std::string protocol;
    std::size_t count{};
    if (!(in >> protocol >> remote.position.x >> remote.position.y >> remote.direction >> remote.size
          >> remote.costume >> remote.visible >> std::quoted(remote.name) >> count) ||
        protocol != "BBDX1" || !bounded(remote.position.x, 50000) || !bounded(remote.position.y, 50000) ||
        !bounded(remote.direction, 360) || remote.size <= 0 || !bounded(remote.size, 10000) ||
        remote.costume < 0 || remote.name.empty() || remote.name.size() > 128 ||
        remote.name.find_first_of("\r\n") != std::string::npos || count > maxAttacks)
        throw std::runtime_error("Invalid multiplayer player state");
    for (std::size_t i = 0; i < count; ++i) {
        NetworkAttack attack;
        unsigned kind{};
        if (!(in >> kind >> attack.item >> attack.origin.x >> attack.origin.y >>
              attack.velocity.x >> attack.velocity.y >> attack.angle >> attack.charge) ||
            kind > static_cast<unsigned>(RemoteAttack::Smoke) || attack.item > 25 ||
            !bounded(attack.origin.x, 50000) || !bounded(attack.origin.y, 50000) ||
            !bounded(attack.velocity.x, 120) || !bounded(attack.velocity.y, 120) ||
            !bounded(attack.angle, 360) || !bounded(attack.charge, 120))
            throw std::runtime_error("Invalid multiplayer attack");
        attack.kind = static_cast<RemoteAttack>(kind);
        remote.attacks.push_back(attack);
    }
    in >> std::ws;
    if (!in.eof()) throw std::runtime_error("Trailing multiplayer frame data");
    return remote;
}
}
