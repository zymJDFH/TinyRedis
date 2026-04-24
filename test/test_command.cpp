#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include "command/inMemoryDB.hpp"
#include "command/commandDispatcher.hpp"
#include "command/commandParser.hpp"
#include "protocol/respParser.hpp"

namespace {
long long parseIntegerReply(const std::string& resp) {
    if (resp.size() < 4 || resp.front() != ':' || resp.back() != '\n') {
        return 0;
    }
    return std::stoll(resp.substr(1, resp.size() - 3));
}
} // namespace

TEST(CommandParserTest, ParseArrayCommand) {
    RESPObject cmd;
    cmd.type = RESPType::ARRAY;

    RESPObject e1;
    e1.type = RESPType::BULK_STRING;
    e1.str = "SET";
    RESPObject e2;
    e2.type = RESPType::BULK_STRING;
    e2.str = "k";
    RESPObject e3;
    e3.type = RESPType::BULK_STRING;
    e3.str = "v";
    cmd.elements = {e1, e2, e3};

    std::vector<std::string> argv;
    std::string err;
    ASSERT_TRUE(CommandParser::toArgv(cmd, argv, err));
    ASSERT_TRUE(err.empty());
    ASSERT_EQ(argv.size(), 3u);
    EXPECT_EQ(argv[0], "SET");
    EXPECT_EQ(argv[1], "k");
    EXPECT_EQ(argv[2], "v");
}

TEST(CommandParserTest, RejectNonArray) {
    RESPObject cmd;
    cmd.type = RESPType::BULK_STRING;
    cmd.str = "PING";

    std::vector<std::string> argv;
    std::string err;
    ASSERT_FALSE(CommandParser::toArgv(cmd, argv, err));
    EXPECT_FALSE(err.empty());
}

TEST(CommandParserTest, RejectEmptyArray) {
    RESPObject cmd;
    cmd.type = RESPType::ARRAY;

    std::vector<std::string> argv;
    std::string err;
    ASSERT_FALSE(CommandParser::toArgv(cmd, argv, err));
    EXPECT_EQ(err, "protocol error: empty command");
}

TEST(CommandParserTest, RejectArrayWithNonStringElement) {
    RESPObject cmd;
    cmd.type = RESPType::ARRAY;

    RESPObject e1;
    e1.type = RESPType::BULK_STRING;
    e1.str = "GET";
    RESPObject e2;
    e2.type = RESPType::INTEGER;
    e2.integer = 1;

    cmd.elements = {e1, e2};

    std::vector<std::string> argv;
    std::string err;
    ASSERT_FALSE(CommandParser::toArgv(cmd, argv, err));
    EXPECT_EQ(err, "protocol error: command element must be string");
}

TEST(CommandParserTest, RejectArrayWithNullBulkElement) {
    RESPObject cmd;
    cmd.type = RESPType::ARRAY;

    RESPObject e1;
    e1.type = RESPType::BULK_STRING;
    e1.str = "GET";
    RESPObject e2;
    e2.type = RESPType::NULL_BULK;

    cmd.elements = {e1, e2};

    std::vector<std::string> argv;
    std::string err;
    ASSERT_FALSE(CommandParser::toArgv(cmd, argv, err));
    EXPECT_EQ(err, "protocol error: command element must be string");
    EXPECT_TRUE(argv.empty());
}

TEST(CommandDispatcherTest, PingSetGetDelFlow) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"PING"}), "+PONG\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PING", "hi"}), "$2\r\nhi\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "name", "tinyredis"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "name"}), "$9\r\ntinyredis\r\n");
    EXPECT_EQ(dispatcher.dispatch({"DEL", "name"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "name"}), "$-1\r\n");
}

TEST(CommandDispatcherTest, CaseInsensitiveCommands) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"set", "k", "v"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"gEt", "k"}), "$1\r\nv\r\n");
    EXPECT_EQ(dispatcher.dispatch({"pInG"}), "+PONG\r\n");
}

TEST(CommandDispatcherTest, ExistsAndIncrFlow) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "counter"}), ":0\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "counter"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "counter"}), ":2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "counter"}), "$1\r\n2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "counter"}), ":1\r\n");
}

TEST(CommandDispatcherTest, DecrAndIncrByFlow) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"DECR", "n"}), ":-1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCRBY", "n", "10"}), ":9\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "n"}), "$1\r\n9\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCRBY", "n", "-3"}), ":6\r\n");

    EXPECT_EQ(dispatcher.dispatch({"SET", "bad", "abc"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"DECR", "bad"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCRBY", "bad", "2"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCRBY", "n", "x"}), "-ERR value is not an integer or out of range\r\n");
}

TEST(CommandDispatcherTest, MSetAndMGetFlow) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"MSET", "k1", "v1", "k2", "v2"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"MGET", "k1", "missing", "k2"}), "*3\r\n$2\r\nv1\r\n$-1\r\n$2\r\nv2\r\n");

    // MSET 覆盖已有值，并清理对应 TTL
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "k1", "10"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"MSET", "k1", "nv1"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"TTL", "k1"}), ":-1\r\n");
}

TEST(CommandDispatcherTest, ExistsSupportsMultipleKeys) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"MSET", "a", "1", "b", "2"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "a", "missing", "b"}), ":2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "a", "a"}), ":2\r\n");
}

TEST(CommandDispatcherTest, EmptyCommandError) {
    CommandDispatcher dispatcher;
    EXPECT_EQ(dispatcher.dispatch({}), "-ERR empty command\r\n");
}

TEST(CommandDispatcherTest, ErrorPaths) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"PING", "a", "b"}), "-ERR wrong number of arguments for 'ping' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "k"}), "-ERR wrong number of arguments for 'set' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"DEL"}), "-ERR wrong number of arguments for 'del' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS"}), "-ERR wrong number of arguments for 'exists' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR"}), "-ERR wrong number of arguments for 'incr' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"DECR"}), "-ERR wrong number of arguments for 'decr' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCRBY"}), "-ERR wrong number of arguments for 'incrby' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"MGET"}), "-ERR wrong number of arguments for 'mget' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"MSET", "k1"}), "-ERR wrong number of arguments for 'mset' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"MSET", "k1", "v1", "k2"}), "-ERR wrong number of arguments for 'mset' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "k"}), "-ERR wrong number of arguments for 'expire' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"TTL"}), "-ERR wrong number of arguments for 'ttl' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PTTL"}), "-ERR wrong number of arguments for 'pttl' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PERSIST"}), "-ERR wrong number of arguments for 'persist' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"REWRITEAOF", "extra"}), "-ERR wrong number of arguments for 'rewriteaof' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"BGREWRITEAOF", "extra"}), "-ERR wrong number of arguments for 'bgrewriteaof' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "k", "abc"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "k", "10x"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "n", "abc"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "n"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCRBY", "n", "2x"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "mx", "9223372036854775807"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "mx"}), "-ERR increment or decrement would overflow\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "mn", "-9223372036854775808"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"DECR", "mn"}), "-ERR increment or decrement would overflow\r\n");
    EXPECT_EQ(dispatcher.dispatch({"UNKNOWN"}), "-ERR unknown command 'UNKNOWN'\r\n");
}

TEST(CommandDispatcherTest, InfoReturnsMetricsAndAofState) {
    ServerMetrics metrics;
    metrics.tcpPort.store(6380);
    metrics.connectedClients.store(2);
    metrics.totalConnectionsReceived.store(5);

    CommandDispatcher dispatcher(true, "/tmp/tinyredis-info-test.aof", AofFsyncPolicy::EverySec, &metrics);

    EXPECT_EQ(dispatcher.dispatch({"PING"}), "+PONG\r\n");
    const std::string reply = dispatcher.dispatch({"INFO"});

    EXPECT_NE(reply.find("# Server\r\n"), std::string::npos);
    EXPECT_NE(reply.find("redis_version:tinyredis\r\n"), std::string::npos);
    EXPECT_NE(reply.find("tcp_port:6380\r\n"), std::string::npos);
    EXPECT_NE(reply.find("# Clients\r\n"), std::string::npos);
    EXPECT_NE(reply.find("connected_clients:2\r\n"), std::string::npos);
    EXPECT_NE(reply.find("total_connections_received:5\r\n"), std::string::npos);
    EXPECT_NE(reply.find("# Stats\r\n"), std::string::npos);
    EXPECT_NE(reply.find("total_commands_processed:2\r\n"), std::string::npos);
    EXPECT_NE(reply.find("# Persistence\r\n"), std::string::npos);
    EXPECT_NE(reply.find("aof_enabled:1\r\n"), std::string::npos);
    EXPECT_NE(reply.find("aof_filename:/tmp/tinyredis-info-test.aof\r\n"), std::string::npos);
    EXPECT_NE(reply.find("aof_fsync:everysec\r\n"), std::string::npos);
    EXPECT_NE(reply.find("aof_rewrite_in_progress:0\r\n"), std::string::npos);
    EXPECT_NE(reply.find("aof_last_bgrewrite_status:ok\r\n"), std::string::npos);
    EXPECT_NE(reply.find("# Replication\r\n"), std::string::npos);
    EXPECT_NE(reply.find("role:master\r\n"), std::string::npos);
    EXPECT_NE(reply.find("connected_slaves:0\r\n"), std::string::npos);
    EXPECT_NE(reply.find("master_repl_offset:0\r\n"), std::string::npos);
}

TEST(CommandDispatcherTest, InfoSupportsSectionFilter) {
    CommandDispatcher dispatcher;
    const std::string reply = dispatcher.dispatch({"INFO", "persistence"});

    EXPECT_NE(reply.find("# Persistence\r\n"), std::string::npos);
    EXPECT_EQ(reply.find("# Server\r\n"), std::string::npos);
    EXPECT_EQ(reply.find("# Clients\r\n"), std::string::npos);
    EXPECT_EQ(dispatcher.dispatch({"INFO", "unknown"}), "-ERR unsupported INFO section\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INFO", "server", "extra"}), "-ERR wrong number of arguments for 'info' command\r\n");
}

TEST(CommandDispatcherTest, InfoReplicationShowsReplicaState) {
    ReplicationState replication;
    replication.becomeReplica("127.0.0.1", 6379);

    CommandDispatcher dispatcher(false, "appendonly.aof", AofFsyncPolicy::No, nullptr, &replication);
    const std::string reply = dispatcher.dispatch({"INFO", "replication"});

    EXPECT_NE(reply.find("# Replication\r\n"), std::string::npos);
    EXPECT_NE(reply.find("role:slave\r\n"), std::string::npos);
    EXPECT_NE(reply.find("master_host:127.0.0.1\r\n"), std::string::npos);
    EXPECT_NE(reply.find("master_port:6379\r\n"), std::string::npos);
    EXPECT_NE(reply.find("master_link_status:down\r\n"), std::string::npos);
    EXPECT_EQ(reply.find("# Server\r\n"), std::string::npos);
}

TEST(CommandDispatcherTest, DelMultipleKeysCountsOnlyExisting) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"SET", "a", "1"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "b", "2"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"DEL", "a", "a", "missing", "b"}), ":2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "a"}), ":0\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "b"}), ":0\r\n");
}

TEST(CommandDispatcherTest, TtlAndPersistFlow) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"TTL", "k"}), ":-2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PTTL", "k"}), ":-2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PERSIST", "k"}), ":0\r\n");

    EXPECT_EQ(dispatcher.dispatch({"SET", "k", "v"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"TTL", "k"}), ":-1\r\n");

    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "k", "10"}), ":1\r\n");
    const long long ttlAfterExpire = parseIntegerReply(dispatcher.dispatch({"TTL", "k"}));
    EXPECT_GE(ttlAfterExpire, 0);
    EXPECT_LE(ttlAfterExpire, 10);

    const long long pttlAfterExpire = parseIntegerReply(dispatcher.dispatch({"PTTL", "k"}));
    EXPECT_GT(pttlAfterExpire, 0);
    EXPECT_LE(pttlAfterExpire, 10000);

    EXPECT_EQ(dispatcher.dispatch({"PERSIST", "k"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"TTL", "k"}), ":-1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PERSIST", "k"}), ":0\r\n");
}

TEST(CommandDispatcherTest, SetClearsExistingTtl) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"SET", "k", "v1"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "k", "10"}), ":1\r\n");
    EXPECT_GE(parseIntegerReply(dispatcher.dispatch({"TTL", "k"})), 0);

    EXPECT_EQ(dispatcher.dispatch({"SET", "k", "v2"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "k"}), "$2\r\nv2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"TTL", "k"}), ":-1\r\n");
}

TEST(CommandDispatcherTest, ExpireImmediatelyDeletesKey) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"SET", "temp", "1"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "temp", "0"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "temp"}), "$-1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"TTL", "temp"}), ":-2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PTTL", "temp"}), ":-2\r\n");
}

TEST(CommandDispatcherTest, ExpireMissingKeyReturnsZero) {
    CommandDispatcher dispatcher;
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "missing", "10"}), ":0\r\n");
}

TEST(CommandDispatcherTest, ExpireNegativeDeletesKey) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"SET", "neg", "1"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXPIRE", "neg", "-3"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "neg"}), ":0\r\n");
}

TEST(InMemoryDBTTLTest, ActiveExpireCycleRemovesExpiredKeys) {
    InMemoryDB db;
    std::string value;

    db.set("active_k", "v");
    EXPECT_EQ(db.expire("active_k", 1), 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));

    EXPECT_EQ(db.activeExpireCycle(64), 1u);
    EXPECT_FALSE(db.get("active_k", value));
    EXPECT_EQ(db.ttl("active_k"), -2);
}

TEST(CommandIntegrationTest, RespToCommandExecution) {
    RESPParser parser;
    CommandDispatcher dispatcher;

    const std::string req = "*3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n";
    parser.feed(req.data(), req.size());

    RESPObject obj;
    ASSERT_TRUE(parser.parse(obj));

    std::vector<std::string> argv;
    std::string err;
    ASSERT_TRUE(CommandParser::toArgv(obj, argv, err));

    const std::string reply = dispatcher.dispatch(argv);
    EXPECT_EQ(reply, "+OK\r\n");
}
