#pragma once

#include "inMemoryDB.hpp"
#include "../metrics/serverMetrics.hpp"
#include "../persistentence/aof.hpp"
#include "../replication/replicationState.hpp"
#include <string>
#include <vector>

class CommandDispatcher {
public:
    CommandDispatcher(bool enableAof = false,
                      std::string aofPath = "appendonly.aof",
                      AofFsyncPolicy fsyncPolicy = AofFsyncPolicy::Always,
                      ServerMetrics* metrics = nullptr,
                      ReplicationState* replication = nullptr);

    std::string dispatch(const std::vector<std::string>& argv);
    bool loadAof();
    bool rewriteAof(std::string& err);
    const std::string& lastError() const;
    bool applyReplicationCommand(const std::vector<std::string>& argv, std::string& err);
    std::string fullResyncPayload();
    bool isReplicableWriteCommand(const std::vector<std::string>& argv) const;
    // 事件循环周期性调用：执行主动过期扫描和 AOF everysec 刷盘。
    void cron();

private:
    std::string dispatchInternal(const std::vector<std::string>& argv, bool replayingAof);
    std::string buildInfoReply(const std::string& section) const;
    std::vector<std::vector<std::string>> snapshotCommands();
    bool isWriteCommand(const std::string& cmd) const;

private:
    InMemoryDB db_;
    AOF aof_;
    ServerMetrics localMetrics_;
    ServerMetrics* metrics_;
    ReplicationState localReplication_;
    ReplicationState* replication_;
    std::string lastError_;
};
