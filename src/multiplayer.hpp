#pragma once
#include "coordinates.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

class PeerConnection {
public:
    PeerConnection(unsigned port, const std::string& host);
    ~PeerConnection();
    PeerConnection(const PeerConnection&) = delete;
    PeerConnection& operator=(const PeerConnection&) = delete;
    std::int64_t epoch() const;
    NetworkFrame exchange(const NetworkFrame& local);
private:
    struct Socket;
    std::unique_ptr<Socket> connection;
};
}
