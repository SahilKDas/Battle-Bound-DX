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
enum class MatchPhase : unsigned { Countdown, Playing, Finished };
struct MatchState {
    unsigned round{1}, map{1};
    MatchPhase phase{MatchPhase::Countdown};
    unsigned ticksLeft{90}, hostScore{}, guestScore{};
    bool hostReady{}, guestReady{};
};
struct NetworkFrame {
    WorldPoint position;
    double direction{}, size{};
    int costume{};
    bool visible{};
    std::string name;
    std::vector<NetworkAttack> attacks;
    MatchState match;
    unsigned reportedRound{1}, deaths{};
    bool rematch{};
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
