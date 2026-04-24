#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <unistd.h>

#include "command/commandDispatcher.hpp"

namespace {
std::string tempAofPath(const std::string& suffix) {
    return "/tmp/tinyredis_test_aof_" + std::to_string(static_cast<long long>(::getpid())) + "_" + suffix + ".aof";
}

void removeAofArtifacts(const std::string& path) {
    (void)std::filesystem::remove(path);
    (void)std::filesystem::remove(path + ".tmp");
    (void)std::filesystem::remove(path + ".tmp.bg");
}

void waitForBackgroundRewrite(CommandDispatcher& dispatcher) {
    for (int i = 0; i < 200; ++i) {
        const std::string info = dispatcher.dispatch({"INFO", "persistence"});
        if (info.find("aof_rewrite_in_progress:0\r\n") != std::string::npos) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        dispatcher.cron();
    }

    FAIL() << "background rewrite did not finish in time";
}
} // namespace

TEST(AOFTest, MissingFileLoadSucceeds) {
    const std::string path = tempAofPath("missing");
    removeAofArtifacts(path);

    CommandDispatcher dispatcher(true, path);
    EXPECT_TRUE(dispatcher.loadAof());
}

TEST(AOFTest, AppendAndReplayRestoresState) {
    const std::string path = tempAofPath("restore");
    removeAofArtifacts(path);

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

    removeAofArtifacts(path);
}

TEST(AOFTest, EverySecPolicyAppendsAndReplays) {
    const std::string path = tempAofPath("everysec");
    removeAofArtifacts(path);

    {
        CommandDispatcher writer(true, path, AofFsyncPolicy::EverySec);
        EXPECT_EQ(writer.dispatch({"SET", "k", "1"}), "+OK\r\n");
        writer.cron();
    }

    {
        CommandDispatcher reader(true, path, AofFsyncPolicy::EverySec);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "k"}), "$1\r\n1\r\n");
    }

    removeAofArtifacts(path);
}

TEST(AOFTest, NoFsyncPolicyStillAppendsAndReplays) {
    const std::string path = tempAofPath("nofsync");
    removeAofArtifacts(path);

    {
        CommandDispatcher writer(true, path, AofFsyncPolicy::No);
        EXPECT_EQ(writer.dispatch({"SET", "k", "v"}), "+OK\r\n");
    }

    {
        CommandDispatcher reader(true, path, AofFsyncPolicy::No);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "k"}), "$1\r\nv\r\n");
    }

    removeAofArtifacts(path);
}

TEST(AOFTest, FailedWriteCommandIsNotAppended) {
    const std::string path = tempAofPath("failed_cmd");
    removeAofArtifacts(path);

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

    removeAofArtifacts(path);
}

TEST(AOFTest, RewriteKeepsFinalState) {
    const std::string path = tempAofPath("rewrite_final");
    removeAofArtifacts(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "k", "1"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"SET", "k", "2"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"INCR", "k"}), ":3\r\n");
        EXPECT_EQ(writer.dispatch({"BGREWRITEAOF"}), "+OK\r\n");
        waitForBackgroundRewrite(writer);
    }

    {
        CommandDispatcher reader(true, path);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "k"}), "$1\r\n3\r\n");
    }

    removeAofArtifacts(path);
}

TEST(AOFTest, RewriteDropsDeletedKeys) {
    const std::string path = tempAofPath("rewrite_deleted");
    removeAofArtifacts(path);

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

    removeAofArtifacts(path);
}

TEST(AOFTest, RewritePreservesTtl) {
    const std::string path = tempAofPath("rewrite_ttl");
    removeAofArtifacts(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "ttl", "v"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"EXPIRE", "ttl", "10"}), ":1\r\n");
        EXPECT_EQ(writer.dispatch({"BGREWRITEAOF"}), "+OK\r\n");
        waitForBackgroundRewrite(writer);
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

    removeAofArtifacts(path);
}

TEST(AOFTest, BackgroundRewriteCapturesLiveWrites) {
    const std::string path = tempAofPath("rewrite_live_writes");
    removeAofArtifacts(path);

    {
        CommandDispatcher writer(true, path);
        EXPECT_EQ(writer.dispatch({"SET", "k", "1"}), "+OK\r\n");
        EXPECT_EQ(writer.dispatch({"BGREWRITEAOF"}), "+OK\r\n");

        const std::string infoWhileRunning = writer.dispatch({"INFO", "persistence"});
        EXPECT_NE(infoWhileRunning.find("aof_rewrite_in_progress:1\r\n"), std::string::npos);
        EXPECT_NE(infoWhileRunning.find("aof_last_bgrewrite_status:in_progress\r\n"), std::string::npos);

        EXPECT_EQ(writer.dispatch({"INCRBY", "k", "4"}), ":5\r\n");
        EXPECT_EQ(writer.dispatch({"SET", "tail", "ok"}), "+OK\r\n");

        waitForBackgroundRewrite(writer);
    }

    {
        CommandDispatcher reader(true, path);
        ASSERT_TRUE(reader.loadAof()) << reader.lastError();
        EXPECT_EQ(reader.dispatch({"GET", "k"}), "$1\r\n5\r\n");
        EXPECT_EQ(reader.dispatch({"GET", "tail"}), "$2\r\nok\r\n");
    }

    removeAofArtifacts(path);
}
