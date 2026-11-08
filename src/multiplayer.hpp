#pragma once
#include "coordinates.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <stop_token>

namespace bb {
enum class RemoteAttack : unsigned {
    Bullet, Arrow, Shuriken, Rocket, Grenade, Rock, Flame, Melee, Taser, Smoke
};
struct NetworkAttack {
    RemoteAttack kind{};
    WorldPoint origin;
    WorldVelocity velocity;
    double angle{}, charge{};
    unsigned item{};
};
struct NetworkFrame {
    WorldPoint position;
    double direction{}, size{};
    int costume{};
    bool visible{};
    std::string name;
    std::vector<NetworkAttack> attacks;
};

struct LobbyPeer { std::string name; bool ready{}; };

class PeerConnection {
public:
    PeerConnection(unsigned port, const std::string& host, bool lobby = false, std::stop_token stop = {});
    ~PeerConnection();
    PeerConnection(const PeerConnection&) = delete;
    PeerConnection& operator=(const PeerConnection&) = delete;
    std::int64_t epoch() const;
    LobbyPeer exchangeLobby(const LobbyPeer& local);
    NetworkFrame exchange(const NetworkFrame& local);
private:
    struct Socket;
    std::unique_ptr<Socket> connection;
};
}
