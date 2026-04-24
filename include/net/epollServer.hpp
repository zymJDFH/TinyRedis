#pragma once

#include "../config/serverConfig.hpp"
#include "../command/commandDispatcher.hpp"
#include "../metrics/serverMetrics.hpp"
#include "clientSession.hpp"
#include "masterReplicationLink.hpp"

#include <string>
#include <unordered_set>
#include <unordered_map>

class EpollServer {
public:
    explicit EpollServer(int port = 6379);
    explicit EpollServer(ServerConfig config);
    ~EpollServer();

    EpollServer(const EpollServer&) = delete;
    EpollServer& operator=(const EpollServer&) = delete;

    bool init();
    void run();

private:
    bool initListenSocket();
    bool initEpoll();
    bool updateClientEvents(int fd, bool wantWrite);
    void closeClient(int fd);

    void acceptClients();
    void handleClientRead(int fd);
    void handleClientWrite(int fd);
    bool initReplication();
    bool connectToMaster();
    void handleMasterRead();
    void handleMasterWrite();
    void closeMaster();
    void broadcastToReplicas(const std::vector<std::string>& argv);
    void removeReplica(int fd);

private:
    int port_;
    int listenFd_;
    int epollFd_;

    ServerConfig config_;
    ServerMetrics metrics_;
    ReplicationState replication_;
    CommandDispatcher dispatcher_;
    std::unordered_map<int, ClientSession> clients_;
    std::unordered_set<int> replicaFds_;
    MasterReplicationLink masterLink_;
};
