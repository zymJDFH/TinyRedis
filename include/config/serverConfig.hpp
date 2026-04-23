#pragma once

#include "../persistentence/aof.hpp"
#include "../replication/replicationState.hpp"

#include <string>

struct ServerConfig {
    int port = 6379;
    bool appendOnly = true;
    std::string appendFilename = "appendonly.aof";
    AofFsyncPolicy appendFsync = AofFsyncPolicy::Always;
    ReplicationState replication;
};

bool loadServerConfig(const std::string& path, ServerConfig& config, std::string& err);
bool parseAofFsyncPolicy(const std::string& value, AofFsyncPolicy& policy);
