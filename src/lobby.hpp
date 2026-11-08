#pragma once
#include "multiplayer.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace bb {
struct LobbyStatus {
    std::string peer, error;
    bool peerReady{}, starting{};
};

class LobbySession {
public:
    LobbySession(unsigned port, std::string host, std::string username);
    ~LobbySession() = default;
    void ready() { localReady = true; }
    bool isReady() const { return localReady; }
    LobbyStatus status() const;
    std::unique_ptr<PeerConnection> takeConnection();

private:
    mutable std::mutex mutex;
    LobbyStatus state;
    std::unique_ptr<PeerConnection> connection;
    std::atomic<bool> localReady{};
    // Destroy and join the worker before destroying any shared state.
    std::jthread worker;
};
}
