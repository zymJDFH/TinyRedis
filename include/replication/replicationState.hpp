#pragma once

#include <string>
#include <utility>

enum class ReplicationRole {
    Master,
    Replica,
};

struct ReplicationState {
    ReplicationRole role = ReplicationRole::Master;
    std::string masterHost;
    int masterPort = 0;
    bool masterLinkUp = false;
    int connectedReplicas = 0;
    std::string masterReplId = "0000000000000000000000000000000000000000";
    long long masterReplOffset = 0;

    void becomeMaster() {
        role = ReplicationRole::Master;
        masterHost.clear();
        masterPort = 0;
        masterLinkUp = false;
    }

    void becomeReplica(std::string host, int port) {
        role = ReplicationRole::Replica;
        masterHost = std::move(host);
        masterPort = port;
        masterLinkUp = false;
    }

    bool isReplica() const {
        return role == ReplicationRole::Replica;
    }
};
