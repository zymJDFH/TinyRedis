#pragma once

#include "../protocol/respParser.hpp"

#include <string>

enum class MasterSyncState {
    Disconnected,
    WaitingPong,
    WaitingReplconf,
    WaitingFullResync,
    Streaming,
};

struct MasterReplicationLink {
    int fd = -1;
    RESPParser parser;
    std::string writeBuf;
    MasterSyncState state = MasterSyncState::Disconnected;
};
