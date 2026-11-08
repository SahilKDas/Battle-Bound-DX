#include "lobby.hpp"
#include <chrono>
#include <stdexcept>

namespace bb {
LobbySession::LobbySession(unsigned port, std::string host, std::string username)
    : worker([this, port, host = std::move(host), username = std::move(username)](std::stop_token stop) {
        try {
            auto peer = std::make_unique<PeerConnection>(port, host, true, stop);
            while (!stop.stop_requested()) {
                const bool readySent = localReady;
                const auto remote = peer->exchangeLobby({username, readySent});
                {
                    std::scoped_lock lock(mutex);
                    if (!state.peer.empty() && state.peer != remote.name)
                        throw std::runtime_error("Peer changed identity during ready-up.");
                    state.peer = remote.name;
                    state.peerReady = remote.ready;
                    if (readySent && remote.ready) {
                        state.starting = true;
                        connection = std::move(peer);
                        return;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        } catch (const std::exception& error) {
            std::scoped_lock lock(mutex);
            state.error = error.what();
        }
    }) {}

LobbyStatus LobbySession::status() const {
    std::scoped_lock lock(mutex);
    return state;
}

std::unique_ptr<PeerConnection> LobbySession::takeConnection() {
    std::scoped_lock lock(mutex);
    return std::move(connection);
}
}
