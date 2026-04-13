#include <gtest/gtest.h>

#include "command/commandDispatcher.hpp"
#include "command/commandParser.hpp"
#include "protocol/respParser.hpp"

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

TEST(CommandDispatcherTest, PingSetGetDelFlow) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"PING"}), "+PONG\r\n");
    EXPECT_EQ(dispatcher.dispatch({"PING", "hi"}), "$2\r\nhi\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "name", "tinyredis"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "name"}), "$9\r\ntinyredis\r\n");
    EXPECT_EQ(dispatcher.dispatch({"DEL", "name"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "name"}), "$-1\r\n");
}

TEST(CommandDispatcherTest, ExistsAndIncrFlow) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "counter"}), ":0\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "counter"}), ":1\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "counter"}), ":2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET", "counter"}), "$1\r\n2\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS", "counter"}), ":1\r\n");
}

TEST(CommandDispatcherTest, ErrorPaths) {
    CommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.dispatch({"SET", "k"}), "-ERR wrong number of arguments for 'set' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"EXISTS"}), "-ERR wrong number of arguments for 'exists' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR"}), "-ERR wrong number of arguments for 'incr' command\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "n", "abc"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "n"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(dispatcher.dispatch({"SET", "mx", "9223372036854775807"}), "+OK\r\n");
    EXPECT_EQ(dispatcher.dispatch({"INCR", "mx"}), "-ERR increment or decrement would overflow\r\n");
    EXPECT_EQ(dispatcher.dispatch({"UNKNOWN"}), "-ERR unknown command 'UNKNOWN'\r\n");
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
