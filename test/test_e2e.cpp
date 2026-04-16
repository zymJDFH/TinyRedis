#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "../include/protocol/respParser.hpp"

namespace {
std::string buildRequest(const std::vector<std::string>& argv) {
    std::string out = "*" + std::to_string(argv.size()) + "\r\n";
    for (const std::string& arg : argv) {
        out += "$" + std::to_string(arg.size()) + "\r\n";
        out += arg;
        out += "\r\n";
    }
    return out;
}

std::string getExecutableDir() {
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) {
        return ".";
    }
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path().string();
}

class ServerProcess {
public:
    ServerProcess() = default;
    ~ServerProcess() {
        stop();
    }

    bool start(const std::string& serverBinary, int port) {
        stop();
        port_ = port;

        pid_ = ::fork();
        if (pid_ < 0) {
            return false;
        }

        if (pid_ == 0) {
            const std::string portStr = std::to_string(port_);
            ::execl(serverBinary.c_str(), serverBinary.c_str(), portStr.c_str(), nullptr);
            _exit(127);
        }

        for (int i = 0; i < 100; ++i) {
            const int fd = connectOnce();
            if (fd >= 0) {
                ::close(fd);
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        stop();
        return false;
    }

    void stop() {
        if (pid_ <= 0) {
            pid_ = -1;
            return;
        }

        ::kill(pid_, SIGTERM);
        for (int i = 0; i < 50; ++i) {
            int status = 0;
            const pid_t w = ::waitpid(pid_, &status, WNOHANG);
            if (w == pid_) {
                pid_ = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        ::kill(pid_, SIGKILL);
        int status = 0;
        (void)::waitpid(pid_, &status, 0);
        pid_ = -1;
    }

private:
    int connectOnce() const {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port_));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }

private:
    pid_t pid_ = -1;
    int port_ = 0;
};

class RespClient {
public:
    RespClient() = default;
    ~RespClient() {
        close();
    }

    bool connectTo(int port) {
        close();

        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            return false;
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close();
            return false;
        }
        return true;
    }

    bool sendAll(const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, 0);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            if (n == 0) {
                return false;
            }
            sent += static_cast<size_t>(n);
        }
        return true;
    }

    bool readReply(RESPObject& out, int timeoutMs = 2000) {
        for (;;) {
            try {
                if (parser_.parse(out)) {
                    return true;
                }
            } catch (const std::exception&) {
                return false;
            }

            pollfd pfd {};
            pfd.fd = fd_;
            pfd.events = POLLIN | POLLHUP | POLLERR;
            const int pr = ::poll(&pfd, 1, timeoutMs);
            if (pr <= 0) {
                return false;
            }

            if ((pfd.revents & (POLLERR | POLLHUP)) != 0) {
                return false;
            }

            char buf[4096];
            const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
            if (n <= 0) {
                return false;
            }
            parser_.feed(buf, static_cast<size_t>(n));
        }
    }

    bool waitClosed(int timeoutMs = 2000) {
        pollfd pfd {};
        pfd.fd = fd_;
        pfd.events = POLLIN | POLLHUP | POLLERR;
        const int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr <= 0) {
            return false;
        }

        if ((pfd.revents & POLLHUP) != 0) {
            return true;
        }
        if ((pfd.revents & POLLIN) != 0) {
            char c = 0;
            const ssize_t n = ::recv(fd_, &c, 1, 0);
            return n == 0;
        }
        return false;
    }

    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
    RESPParser parser_;
};

class TinyRedisE2ETest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        tcpAllowed_ = canUseTcpSocket();
        if (!tcpAllowed_) {
            return;
        }

        const std::string serverBinary = getExecutableDir() + "/tinyredis";
        serverStarted_ = server_.start(serverBinary, kPort);
    }

    static void TearDownTestSuite() {
        if (serverStarted_) {
            server_.stop();
            serverStarted_ = false;
        }
    }

    void SetUp() override {
        if (!tcpAllowed_) {
            GTEST_SKIP() << "TCP socket is not allowed in current environment";
        }
        ASSERT_TRUE(serverStarted_) << "failed to start tinyredis server process";
    }

    static constexpr int kPort = 6391;

private:
    static bool canUseTcpSocket() {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }
        ::close(fd);
        return true;
    }

    static inline ServerProcess server_;
    static inline bool tcpAllowed_ = false;
    static inline bool serverStarted_ = false;
};

TEST_F(TinyRedisE2ETest, BasicCommandFlow) {
    RespClient client;
    ASSERT_TRUE(client.connectTo(kPort));

    ASSERT_TRUE(client.sendAll(buildRequest({"PING"})));
    RESPObject out;
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::SIMPLE_STRING);
    EXPECT_EQ(out.str, "PONG");

    ASSERT_TRUE(client.sendAll(buildRequest({"SET", "name", "tinyredis"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::SIMPLE_STRING);
    EXPECT_EQ(out.str, "OK");

    ASSERT_TRUE(client.sendAll(buildRequest({"GET", "name"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::BULK_STRING);
    EXPECT_EQ(out.str, "tinyredis");

    ASSERT_TRUE(client.sendAll(buildRequest({"DEL", "name"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::INTEGER);
    EXPECT_EQ(out.integer, 1);

    ASSERT_TRUE(client.sendAll(buildRequest({"GET", "name"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::NULL_BULK);
}

TEST_F(TinyRedisE2ETest, TtlExpireAndPersistFlow) {
    RespClient client;
    ASSERT_TRUE(client.connectTo(kPort));

    ASSERT_TRUE(client.sendAll(buildRequest({"SET", "ttl_k", "v"})));
    RESPObject out;
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::SIMPLE_STRING);

    ASSERT_TRUE(client.sendAll(buildRequest({"EXPIRE", "ttl_k", "1"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::INTEGER);
    ASSERT_EQ(out.integer, 1);

    ASSERT_TRUE(client.sendAll(buildRequest({"PTTL", "ttl_k"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::INTEGER);
    EXPECT_GT(out.integer, 0);
    EXPECT_LE(out.integer, 1100);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    ASSERT_TRUE(client.sendAll(buildRequest({"TTL", "ttl_k"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::INTEGER);
    EXPECT_EQ(out.integer, -2);

    ASSERT_TRUE(client.sendAll(buildRequest({"SET", "persist_k", "v"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::SIMPLE_STRING);

    ASSERT_TRUE(client.sendAll(buildRequest({"EXPIRE", "persist_k", "10"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::INTEGER);
    ASSERT_EQ(out.integer, 1);

    ASSERT_TRUE(client.sendAll(buildRequest({"PERSIST", "persist_k"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::INTEGER);
    ASSERT_EQ(out.integer, 1);

    ASSERT_TRUE(client.sendAll(buildRequest({"TTL", "persist_k"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::INTEGER);
    EXPECT_EQ(out.integer, -1);
}

TEST_F(TinyRedisE2ETest, PipelineTwoCommandsInOneWrite) {
    RespClient client;
    ASSERT_TRUE(client.connectTo(kPort));

    const std::string req = buildRequest({"PING"}) + buildRequest({"INCR", "pipe_counter"});
    ASSERT_TRUE(client.sendAll(req));

    RESPObject out1;
    ASSERT_TRUE(client.readReply(out1));
    ASSERT_EQ(out1.type, RESPType::SIMPLE_STRING);
    EXPECT_EQ(out1.str, "PONG");

    RESPObject out2;
    ASSERT_TRUE(client.readReply(out2));
    ASSERT_EQ(out2.type, RESPType::INTEGER);
    EXPECT_GE(out2.integer, 1);
}

TEST_F(TinyRedisE2ETest, SplitSingleCommandAcrossTwoWrites) {
    RespClient client;
    ASSERT_TRUE(client.connectTo(kPort));

    // 将一个完整 SET 命令拆成两段发送，模拟半包场景。
    const std::string part1 = "*3\r\n$3\r\nSET\r\n$6\r\nhalf_k\r\n$1\r\n";
    const std::string part2 = "v\r\n";
    ASSERT_TRUE(client.sendAll(part1));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ASSERT_TRUE(client.sendAll(part2));

    RESPObject out;
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::SIMPLE_STRING);
    EXPECT_EQ(out.str, "OK");

    ASSERT_TRUE(client.sendAll(buildRequest({"GET", "half_k"})));
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::BULK_STRING);
    EXPECT_EQ(out.str, "v");
}

TEST_F(TinyRedisE2ETest, ProtocolErrorThenConnectionClosed) {
    RespClient client;
    ASSERT_TRUE(client.connectTo(kPort));

    ASSERT_TRUE(client.sendAll("?\r\n"));

    RESPObject out;
    ASSERT_TRUE(client.readReply(out));
    ASSERT_EQ(out.type, RESPType::ERROR);
    EXPECT_NE(out.str.find("ERR protocol error"), std::string::npos);

    EXPECT_TRUE(client.waitClosed());
}
} // namespace
