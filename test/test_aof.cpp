#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <unistd.h>

#include "command/commandDispatcher.hpp"

namespace {
std::string tempAofPath(const std::string& suffix) {
    return "/tmp/tinyredis_test_aof_" + std::to_string(static_cast<long long>(::getpid())) + "_" + suffix + ".aof";
}
} // namespace

TEST(AOFTest, MissingFileLoadSucceeds) {
    const std::string path = tempAofPath("missing");
    (void)std::filesystem::remove(path);

    CommandDispatcher dispatcher(true, path);
    EXPECT_TRUE(dispatcher.loadAof());
}

TEST(AOFTest, AppendAndReplayRestoresState) {
    const std::string path = tempAofPath("restore");
    (void)std::filesystem::remove(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "k", "1"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"INCRBY", "k", "5"}), ":6\r\n");
        EXPECT_EQ(writer.dispatch({"MSET", "a", "x", "b", "y"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"EXISTS", "k", "a", "missing"}), ":2\r\n");
    }

    {
        CommandDispatcher reader(true, path);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "k"}), "$1\r\n6\r\n");
        EXPECT_EQ(reader.dispatch({"MGET", "a", "b", "missing"}),
                  "*3\r\n$1\r\nx\r\n$1\r\ny\r\n$-1\r\n");
    }

    (void)std::filesystem::remove(path);
}

TEST(AOFTest, FailedWriteCommandIsNotAppended) {
    const std::string path = tempAofPath("failed_cmd");
    (void)std::filesystem::remove(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "n", "abc"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"INCR", "n"}), "-ERR value is not an integer or out of range\r\n");
    }

    {
        CommandDispatcher reader(true, path);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "n"}), "$3\r\nabc\r\n");
    }

    (void)std::filesystem::remove(path);
}

TEST(AOFTest, RewriteKeepsFinalState) {
    const std::string path = tempAofPath("rewrite_final");
    (void)std::filesystem::remove(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "k", "1"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"SET", "k", "2"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"INCR", "k"}), ":3\r\n");
        EXPECT_EQ(writer.dispatch({"BGREWRITEAOF"}), "+OK\r\n");
    }

    {
        CommandDispatcher reader(true, path);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "k"}), "$1\r\n3\r\n");
    }

    (void)std::filesystem::remove(path);
}

TEST(AOFTest, RewriteDropsDeletedKeys) {
    const std::string path = tempAofPath("rewrite_deleted");
    (void)std::filesystem::remove(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "gone", "1"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"SET", "alive", "2"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"DEL", "gone"}), ":1\r\n");
        EXPECT_EQ(writer.dispatch({"REWRITEAOF"}), "+OK\r\n");
    }

    {
        CommandDispatcher reader(true, path);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "gone"}), "$-1\r\n");
        EXPECT_EQ(reader.dispatch({"GET", "alive"}), "$1\r\n2\r\n");
    }

    (void)std::filesystem::remove(path);
}

TEST(AOFTest, RewritePreservesTtl) {
    const std::string path = tempAofPath("rewrite_ttl");
    (void)std::filesystem::remove(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "ttl", "v"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"EXPIRE", "ttl", "10"}), ":1\r\n");
        EXPECT_EQ(writer.dispatch({"BGREWRITEAOF"}), "+OK\r\n");
    }

    {
        CommandDispatcher reader(true, path);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "ttl"}), "$1\r\nv\r\n");
        const std::string ttlReply = reader.dispatch({"TTL", "ttl"});
        ASSERT_FALSE(ttlReply.empty());
        EXPECT_EQ(ttlReply.front(), ':');
        EXPECT_NE(ttlReply, ":-1\r\n");
        EXPECT_NE(ttlReply, ":-2\r\n");
    }

    (void)std::filesystem::remove(path);
}
