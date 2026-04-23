#include <gtest/gtest.h>

#include "config/serverConfig.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
std::string tempConfigPath(const std::string& suffix) {
    return "/tmp/tinyredis_test_config_" + std::to_string(static_cast<long long>(::getpid())) + "_" + suffix + ".conf";
}
} // namespace

TEST(ServerConfigTest, LoadSupportedDirectives) {
    const std::string path = tempConfigPath("valid");
    {
        std::ofstream out(path);
        out << "# tinyredis config\n";
        out << "port 6380\n";
        out << "appendonly no\n";
        out << "appendfilename /tmp/tinyredis-test.aof\n";
        out << "appendfsync everysec\n";
        out << "replicaof 127.0.0.1 6379\n";
    }

    ServerConfig config;
    std::string err;
    ASSERT_TRUE(loadServerConfig(path, config, err)) << err;

    EXPECT_EQ(config.port, 6380);
    EXPECT_FALSE(config.appendOnly);
    EXPECT_EQ(config.appendFilename, "/tmp/tinyredis-test.aof");
    EXPECT_EQ(config.appendFsync, AofFsyncPolicy::EverySec);
    EXPECT_TRUE(config.replication.isReplica());
    EXPECT_EQ(config.replication.masterHost, "127.0.0.1");
    EXPECT_EQ(config.replication.masterPort, 6379);

    (void)std::filesystem::remove(path);
}

TEST(ServerConfigTest, RejectInvalidAppendFsync) {
    const std::string path = tempConfigPath("bad_fsync");
    {
        std::ofstream out(path);
        out << "appendfsync sometimes\n";
    }

    ServerConfig config;
    std::string err;
    EXPECT_FALSE(loadServerConfig(path, config, err));
    EXPECT_NE(err.find("appendfsync expects always/everysec/no"), std::string::npos);

    (void)std::filesystem::remove(path);
}

TEST(ServerConfigTest, RejectUnknownDirective) {
    const std::string path = tempConfigPath("unknown");
    {
        std::ofstream out(path);
        out << "unknown yes\n";
    }

    ServerConfig config;
    std::string err;
    EXPECT_FALSE(loadServerConfig(path, config, err));
    EXPECT_NE(err.find("unknown directive"), std::string::npos);

    (void)std::filesystem::remove(path);
}

TEST(ServerConfigTest, RejectInvalidReplicaof) {
    const std::string path = tempConfigPath("bad_replicaof");
    {
        std::ofstream out(path);
        out << "replicaof 127.0.0.1 abc\n";
    }

    ServerConfig config;
    std::string err;
    EXPECT_FALSE(loadServerConfig(path, config, err));
    EXPECT_NE(err.find("invalid replicaof port"), std::string::npos);

    (void)std::filesystem::remove(path);
}
