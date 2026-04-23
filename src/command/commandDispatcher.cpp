#include "../../include/command/commandDispatcher.hpp"

#include "../../include/protocol/respEncoder.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <utility>

namespace {
constexpr size_t kActiveExpireSampleCount = 64;

struct MGetValue {
    bool exists;
    std::string value;
};

std::string toUpperCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

std::string wrongArity(const std::string& cmd) {
    return RESPEncoder::error("ERR wrong number of arguments for '" + cmd + "' command");
}

bool parseInt64(const std::string& s, long long& out) {
    // Redis 风格数字参数解析：必须完整匹配整个字符串。
    try {
        size_t parsed = 0;
        const long long value = std::stoll(s, &parsed, 10);
        if (parsed != s.size()) {
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string encodeMGetReply(const std::vector<MGetValue>& values) {
    std::string out = "*" + std::to_string(values.size()) + "\r\n";
    for (const MGetValue& v : values) {
        if (!v.exists) {
            out += RESPEncoder::nullBulk();
        } else {
            out += RESPEncoder::bulkString(v.value);
        }
    }
    return out;
}

bool sectionMatches(const std::string& requested, const std::string& section) {
    return requested.empty() || requested == "ALL" || requested == "DEFAULT" || requested == section;
}
} // namespace

CommandDispatcher::CommandDispatcher(bool enableAof,
                                     std::string aofPath,
                                     AofFsyncPolicy fsyncPolicy,
                                     ServerMetrics* metrics,
                                     ReplicationState* replication)
    : db_(),
      aof_(enableAof, std::move(aofPath), fsyncPolicy),
      localMetrics_(),
      metrics_(metrics == nullptr ? &localMetrics_ : metrics),
      localReplication_(),
      replication_(replication == nullptr ? &localReplication_ : replication),
      lastError_() {}

bool CommandDispatcher::loadAof() {
    lastError_.clear();

    return aof_.replay(
        [&](const std::vector<std::string>& argv, std::string& err) {
            const std::string reply = dispatchInternal(argv, true);
            if (!reply.empty() && reply[0] == '-') {
                err = reply;
                return false;
            }
            return true;
        },
        lastError_);
}

const std::string& CommandDispatcher::lastError() const {
    return lastError_;
}

bool CommandDispatcher::rewriteAof(std::string& err) {
    err.clear();
    return aof_.rewriteCommands(snapshotCommands(), err);
}

std::vector<std::vector<std::string>> CommandDispatcher::snapshotCommands() {
    std::vector<DBSnapshotEntry> entries = db_.snapshot();
    std::vector<std::vector<std::string>> commands;
    commands.reserve(entries.size() * 2);

    for (const DBSnapshotEntry& entry : entries) {
        commands.push_back({"SET", entry.key, entry.value});
        if (entry.ttlMs >= 0) {
            long long ttlSeconds = (entry.ttlMs + 999) / 1000;
            if (ttlSeconds <= 0) {
                ttlSeconds = 1;
            }
            commands.push_back({"EXPIRE", entry.key, std::to_string(ttlSeconds)});
        }
    }

    return commands;
}

std::string CommandDispatcher::fullResyncPayload() {
    std::string out = RESPEncoder::simpleString(
        "FULLRESYNC " + replication_->masterReplId + " " + std::to_string(replication_->masterReplOffset));
    for (const std::vector<std::string>& argv : snapshotCommands()) {
        out += RESPEncoder::array(argv);
    }
    return out;
}

bool CommandDispatcher::applyReplicationCommand(const std::vector<std::string>& argv, std::string& err) {
    err.clear();
    const std::string reply = dispatchInternal(argv, true);
    if (!reply.empty() && reply[0] == '-') {
        err = reply;
        return false;
    }
    return true;
}

bool CommandDispatcher::isWriteCommand(const std::string& cmd) const {
    return cmd == "SET" ||
           cmd == "MSET" ||
           cmd == "DEL" ||
           cmd == "INCR" ||
           cmd == "INCRBY" ||
           cmd == "DECR" ||
           cmd == "EXPIRE" ||
           cmd == "PERSIST";
}

bool CommandDispatcher::isReplicableWriteCommand(const std::vector<std::string>& argv) const {
    if (argv.empty()) {
        return false;
    }
    return isWriteCommand(toUpperCopy(argv[0]));
}

void CommandDispatcher::cron() {
    (void)db_.activeExpireCycle(kActiveExpireSampleCount);

    std::string err;
    if (!aof_.flushIfNeeded(err)) {
        lastError_ = "AOF fsync failed: " + err;
    }
}

std::string CommandDispatcher::dispatch(const std::vector<std::string>& argv) {
    metrics_->onCommandProcessed();
    return dispatchInternal(argv, false);
}

std::string CommandDispatcher::buildInfoReply(const std::string& section) const {
    std::ostringstream out;

    if (sectionMatches(section, "SERVER")) {
        out << "# Server\r\n";
        out << "redis_version:tinyredis\r\n";
        out << "process_id:" << static_cast<long long>(::getpid()) << "\r\n";
        out << "tcp_port:" << metrics_->tcpPort.load(std::memory_order_relaxed) << "\r\n";
        out << "uptime_in_seconds:" << metrics_->uptimeSeconds() << "\r\n";
        out << "\r\n";
    }

    if (sectionMatches(section, "CLIENTS")) {
        out << "# Clients\r\n";
        out << "connected_clients:" << metrics_->connectedClients.load(std::memory_order_relaxed) << "\r\n";
        out << "total_connections_received:"
            << metrics_->totalConnectionsReceived.load(std::memory_order_relaxed) << "\r\n";
        out << "\r\n";
    }

    if (sectionMatches(section, "STATS")) {
        out << "# Stats\r\n";
        out << "total_commands_processed:"
            << metrics_->totalCommandsProcessed.load(std::memory_order_relaxed) << "\r\n";
        out << "\r\n";
    }

    if (sectionMatches(section, "PERSISTENCE")) {
        out << "# Persistence\r\n";
        out << "aof_enabled:" << (aof_.enabled() ? 1 : 0) << "\r\n";
        out << "aof_filename:" << aof_.path() << "\r\n";
        out << "aof_fsync:" << aofFsyncPolicyName(aof_.fsyncPolicy()) << "\r\n";
        out << "\r\n";
    }

    if (sectionMatches(section, "REPLICATION")) {
        out << "# Replication\r\n";
        if (replication_->isReplica()) {
            out << "role:slave\r\n";
            out << "master_host:" << replication_->masterHost << "\r\n";
            out << "master_port:" << replication_->masterPort << "\r\n";
            out << "master_link_status:" << (replication_->masterLinkUp ? "up" : "down") << "\r\n";
        } else {
            out << "role:master\r\n";
            out << "connected_slaves:" << replication_->connectedReplicas << "\r\n";
            out << "master_replid:" << replication_->masterReplId << "\r\n";
            out << "master_repl_offset:" << replication_->masterReplOffset << "\r\n";
        }
        out << "\r\n";
    }

    return RESPEncoder::bulkString(out.str());
}

std::string CommandDispatcher::dispatchInternal(const std::vector<std::string>& argv, bool replayingAof) {
    if (argv.empty()) {
        return RESPEncoder::error("ERR empty command");
    }

    auto appendIfNeeded = [&](bool isWriteCommand) -> std::string {
        if (!isWriteCommand || replayingAof || !aof_.enabled()) {
            return "";
        }

        std::string err;
        if (!aof_.appendCommand(argv, err)) {
            return RESPEncoder::error("ERR AOF append failed: " + err);
        }
        return "";
    };

    const std::string cmd = toUpperCopy(argv[0]);

    if (!replayingAof && replication_->isReplica() && isWriteCommand(cmd)) {
        return RESPEncoder::error("READONLY You can't write against a read only replica");
    }

    if (cmd == "PING") {
        if (argv.size() == 1) {
            return RESPEncoder::simpleString("PONG");
        }
        if (argv.size() == 2) {
            return RESPEncoder::bulkString(argv[1]);
        }
        return wrongArity("ping");
    }

    if (cmd == "SET") {
        if (argv.size() != 3) {
            return wrongArity("set");
        }
        db_.set(argv[1], argv[2]);
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::simpleString("OK");
    }

    if (cmd == "MSET") {
        if (argv.size() < 3 || (argv.size() % 2) == 0) {
            return wrongArity("mset");
        }
        for (size_t i = 1; i + 1 < argv.size(); i += 2) {
            db_.set(argv[i], argv[i + 1]);
        }
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::simpleString("OK");
    }

    if (cmd == "GET") {
        if (argv.size() != 2) {
            return wrongArity("get");
        }
        std::string value;
        if (!db_.get(argv[1], value)) {
            return RESPEncoder::nullBulk();
        }
        return RESPEncoder::bulkString(value);
    }

    if (cmd == "MGET") {
        if (argv.size() < 2) {
            return wrongArity("mget");
        }
        std::vector<MGetValue> values;
        values.reserve(argv.size() - 1);
        for (size_t i = 1; i < argv.size(); ++i) {
            std::string value;
            if (!db_.get(argv[i], value)) {
                values.push_back(MGetValue {false, ""});
            } else {
                values.push_back(MGetValue {true, std::move(value)});
            }
        }
        return encodeMGetReply(values);
    }

    if (cmd == "DEL") {
        if (argv.size() < 2) {
            return wrongArity("del");
        }
        long long removed = 0;
        for (size_t i = 1; i < argv.size(); ++i) {
            removed += db_.del(argv[i]);
        }
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::integer(removed);
    }

    if (cmd == "EXISTS") {
        if (argv.size() < 2) {
            return wrongArity("exists");
        }
        long long existed = 0;
        for (size_t i = 1; i < argv.size(); ++i) {
            existed += db_.exists(argv[i]) ? 1 : 0;
        }
        return RESPEncoder::integer(existed);
    }

    if (cmd == "INCR") {
        if (argv.size() != 2) {
            return wrongArity("incr");
        }

        long long newValue = 0;
        std::string err;
        if (!db_.incrBy(argv[1], 1, newValue, err)) {
            return RESPEncoder::error("ERR " + err);
        }
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::integer(newValue);
    }

    if (cmd == "DECR") {
        if (argv.size() != 2) {
            return wrongArity("decr");
        }

        long long newValue = 0;
        std::string err;
        if (!db_.incrBy(argv[1], -1, newValue, err)) {
            return RESPEncoder::error("ERR " + err);
        }
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::integer(newValue);
    }

    if (cmd == "INCRBY") {
        if (argv.size() != 3) {
            return wrongArity("incrby");
        }

        long long delta = 0;
        if (!parseInt64(argv[2], delta)) {
            return RESPEncoder::error("ERR value is not an integer or out of range");
        }

        long long newValue = 0;
        std::string err;
        if (!db_.incrBy(argv[1], delta, newValue, err)) {
            return RESPEncoder::error("ERR " + err);
        }
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::integer(newValue);
    }

    if (cmd == "EXPIRE") {
        if (argv.size() != 3) {
            return wrongArity("expire");
        }

        long long ttlSeconds = 0;
        if (!parseInt64(argv[2], ttlSeconds)) {
            return RESPEncoder::error("ERR value is not an integer or out of range");
        }

        const int result = db_.expire(argv[1], ttlSeconds);
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::integer(result);
    }

    if (cmd == "TTL") {
        if (argv.size() != 2) {
            return wrongArity("ttl");
        }
        return RESPEncoder::integer(db_.ttl(argv[1]));
    }

    if (cmd == "PTTL") {
        if (argv.size() != 2) {
            return wrongArity("pttl");
        }
        return RESPEncoder::integer(db_.pttl(argv[1]));
    }

    if (cmd == "PERSIST") {
        if (argv.size() != 2) {
            return wrongArity("persist");
        }
        const int result = db_.persist(argv[1]);
        if (const std::string appendErr = appendIfNeeded(true); !appendErr.empty()) {
            return appendErr;
        }
        return RESPEncoder::integer(result);
    }

    if (cmd == "INFO") {
        if (argv.size() > 2) {
            return wrongArity("info");
        }

        const std::string section = argv.size() == 2 ? toUpperCopy(argv[1]) : "";
        if (!section.empty() &&
            section != "ALL" &&
            section != "DEFAULT" &&
            section != "SERVER" &&
            section != "CLIENTS" &&
            section != "STATS" &&
            section != "PERSISTENCE" &&
            section != "REPLICATION") {
            return RESPEncoder::error("ERR unsupported INFO section");
        }

        return buildInfoReply(section);
    }

    if (cmd == "REWRITEAOF" || cmd == "BGREWRITEAOF") {
        if (argv.size() != 1) {
            return wrongArity(cmd == "REWRITEAOF" ? "rewriteaof" : "bgrewriteaof");
        }
        std::string err;
        if (!rewriteAof(err)) {
            return RESPEncoder::error("ERR AOF rewrite failed: " + err);
        }
        return RESPEncoder::simpleString("OK");
    }

    return RESPEncoder::error("ERR unknown command '" + argv[0] + "'");
}
