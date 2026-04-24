#include "../../include/net/epollServer.hpp"

#include "../../include/command/commandParser.hpp"
#include "../../include/net/socketUtil.hpp"
#include "../../include/protocol/respEncoder.hpp"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>
#include <vector>

namespace {
constexpr int kBacklog = 128;
constexpr int kMaxEvents = 128;
constexpr int kEpollWaitTimeoutMs = 100;
constexpr int kCronIntervalMs = 100;
constexpr size_t kReadBufSize = 4096;

ServerConfig defaultConfigWithPort(int port) {
    ServerConfig config;
    config.port = port;
    return config;
}

std::string toUpperCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

bool isOkReply(const std::string& reply) {
    return !reply.empty() && reply[0] != '-';
}
} // namespace

EpollServer::EpollServer(int port)
    : EpollServer(defaultConfigWithPort(port)) {}

EpollServer::EpollServer(ServerConfig config)
    : port_(config.port),
      listenFd_(-1),
      epollFd_(-1),
      config_(std::move(config)),
      metrics_(),
      replication_(config_.replication),
      dispatcher_(config_.appendOnly, config_.appendFilename, config_.appendFsync, &metrics_, &replication_),
      replicaFds_(),
      masterLink_() {
    metrics_.tcpPort.store(port_, std::memory_order_relaxed);
}

EpollServer::~EpollServer() {
    for (const auto& [fd, _] : clients_) {
        (void)_;
        ::close(fd);
    }
    clients_.clear();

    if (listenFd_ >= 0) {
        ::close(listenFd_);
        listenFd_ = -1;
    }
    if (epollFd_ >= 0) {
        ::close(epollFd_);
        epollFd_ = -1;
    }
    if (masterLink_.fd >= 0) {
        ::close(masterLink_.fd);
        masterLink_.fd = -1;
    }
}

bool EpollServer::initListenSocket() {
    std::string err;
    listenFd_ = SocketUtil::createLoopbackListenSocket(port_, kBacklog, err);
    if (listenFd_ < 0) {
        std::cerr << "initListenSocket failed: " << err << "\n";
        return false;
    }

    return true;
}

bool EpollServer::initEpoll() {
    epollFd_ = ::epoll_create1(0);
    if (epollFd_ < 0) {
        std::cerr << "epoll_create1 failed: " << std::strerror(errno) << "\n";
        return false;
    }

    epoll_event ev {};
    ev.events = EPOLLIN;
    ev.data.fd = listenFd_;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenFd_, &ev) < 0) {
        std::cerr << "epoll_ctl add listen fd failed: " << std::strerror(errno) << "\n";
        return false;
    }

    return true;
}

bool EpollServer::init() {
    if (!dispatcher_.loadAof()) {
        std::cerr << "AOF load failed: " << dispatcher_.lastError() << "\n";
        return false;
    }

    if (!initListenSocket()) {
        return false;
    }
    if (!initEpoll()) {
        return false;
    }
    if (!initReplication()) {
        return false;
    }
    std::cout << "TinyRedis(epoll LT) listening on 127.0.0.1:" << port_
              << ", appendonly=" << (config_.appendOnly ? "yes" : "no")
              << ", appendfilename=" << config_.appendFilename
              << ", appendfsync=" << aofFsyncPolicyName(config_.appendFsync)
              << ", role=" << (replication_.isReplica() ? "slave" : "master") << "\n";
    return true;
}

bool EpollServer::updateClientEvents(int fd, bool wantWrite) {
    epoll_event ev {};
    ev.events = EPOLLIN | EPOLLRDHUP;
    if (wantWrite) {
        ev.events |= EPOLLOUT;
    }
    ev.data.fd = fd;

    return ::epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

void EpollServer::closeClient(int fd) {
    ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
    removeReplica(fd);
    if (clients_.erase(fd) > 0) {
        metrics_.onConnectionClosed();
    }
    ::close(fd);
}

void EpollServer::acceptClients() {
    for (;;) {
        const int fd = ::accept(listenFd_, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << "\n";
            break;
        }

        std::string err;
        if (!SocketUtil::setNonBlocking(fd, err)) {
            ::close(fd);
            continue;
        }

        epoll_event ev {};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = fd;
        if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            std::cerr << "epoll_ctl add client failed: " << std::strerror(errno) << "\n";
            ::close(fd);
            continue;
        }

        clients_.emplace(fd, ClientSession {});
        metrics_.onConnectionAccepted();
    }
}

void EpollServer::handleClientRead(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    ClientSession& session = it->second;
    char buf[kReadBufSize];

    for (;;) {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
            closeClient(fd);
            return;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            closeClient(fd);
            return;
        }

        session.parser.feed(buf, static_cast<size_t>(n));

        for (;;) {
            RESPObject obj;
            bool ok = false;
            try {
                ok = session.parser.parse(obj);
            } catch (const std::exception& ex) {
                session.writeBuf += RESPEncoder::error(std::string("ERR protocol error: ") + ex.what());
                session.closeAfterWrite = true;
                break;
            }

            if (!ok) {
                break;
            }

            std::vector<std::string> argv;
            std::string err;
            if (!CommandParser::toArgv(obj, argv, err)) {
                session.writeBuf += RESPEncoder::error("ERR " + err);
                continue;
            }

            const std::string cmd = argv.empty() ? "" : toUpperCopy(argv[0]);
            if (cmd == "REPLCONF") {
                session.writeBuf += RESPEncoder::simpleString("OK");
                continue;
            }

            if (cmd == "PSYNC") {
                session.replica = true;
                replicaFds_.insert(fd);
                replication_.connectedReplicas = static_cast<int>(replicaFds_.size());
                session.writeBuf += dispatcher_.fullResyncPayload();
                continue;
            }

            const std::string reply = dispatcher_.dispatch(argv);
            session.writeBuf += reply;
            if (!session.replica &&
                !replication_.isReplica() &&
                dispatcher_.isReplicableWriteCommand(argv) &&
                isOkReply(reply)) {
                broadcastToReplicas(argv);
            }
        }
    }

    if (!session.writeBuf.empty() && !updateClientEvents(fd, true)) {
        closeClient(fd);
    }
}

void EpollServer::handleClientWrite(int fd) {
    auto it = clients_.find(fd);
    if (it == clients_.end()) {
        return;
    }

    ClientSession& session = it->second;
    while (!session.writeBuf.empty()) {
        const ssize_t n = ::send(fd, session.writeBuf.data(), session.writeBuf.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            closeClient(fd);
            return;
        }
        if (n == 0) {
            closeClient(fd);
            return;
        }

        session.writeBuf.erase(0, static_cast<size_t>(n));
    }

    if (session.writeBuf.empty()) {
        if (session.closeAfterWrite) {
            closeClient(fd);
            return;
        }
        if (!updateClientEvents(fd, false)) {
            closeClient(fd);
        }
    }
}

bool EpollServer::initReplication() {
    if (!replication_.isReplica()) {
        return true;
    }
    return connectToMaster();
}

bool EpollServer::connectToMaster() {
    std::string err;
    masterLink_.fd = SocketUtil::createReplicaConnection(replication_.masterHost, replication_.masterPort, err);
    if (masterLink_.fd < 0) {
        std::cerr << "connect master " << replication_.masterHost << ":" << replication_.masterPort
                  << " failed: " << err << "\n";
        closeMaster();
        return false;
    }

    masterLink_.writeBuf += RESPEncoder::array({"PING"});
    masterLink_.writeBuf += RESPEncoder::array({"REPLCONF", "listening-port", std::to_string(port_)});
    masterLink_.writeBuf += RESPEncoder::array({"PSYNC", "?", "-1"});
    masterLink_.state = MasterSyncState::WaitingPong;

    epoll_event ev {};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
    ev.data.fd = masterLink_.fd;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, masterLink_.fd, &ev) < 0) {
        std::cerr << "epoll_ctl add master fd failed: " << std::strerror(errno) << "\n";
        closeMaster();
        return false;
    }

    return true;
}

void EpollServer::handleMasterWrite() {
    while (!masterLink_.writeBuf.empty()) {
        const ssize_t n = ::send(masterLink_.fd, masterLink_.writeBuf.data(), masterLink_.writeBuf.size(), 0);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            closeMaster();
            return;
        }
        if (n == 0) {
            closeMaster();
            return;
        }
        masterLink_.writeBuf.erase(0, static_cast<size_t>(n));
    }

    if (masterLink_.writeBuf.empty()) {
        epoll_event ev {};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = masterLink_.fd;
        if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, masterLink_.fd, &ev) < 0) {
            closeMaster();
        }
    }
}

void EpollServer::handleMasterRead() {
    char buf[kReadBufSize];

    for (;;) {
        const ssize_t n = ::recv(masterLink_.fd, buf, sizeof(buf), 0);
        if (n == 0) {
            closeMaster();
            return;
        }
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            closeMaster();
            return;
        }

        masterLink_.parser.feed(buf, static_cast<size_t>(n));

        for (;;) {
            RESPObject obj;
            bool ok = false;
            try {
                ok = masterLink_.parser.parse(obj);
            } catch (const std::exception& ex) {
                std::cerr << "replication protocol error: " << ex.what() << "\n";
                closeMaster();
                return;
            }
            if (!ok) {
                break;
            }

            if (masterLink_.state == MasterSyncState::WaitingPong) {
                if (obj.type != RESPType::SIMPLE_STRING || obj.str != "PONG") {
                    closeMaster();
                    return;
                }
                masterLink_.state = MasterSyncState::WaitingReplconf;
                continue;
            }

            if (masterLink_.state == MasterSyncState::WaitingReplconf) {
                if (obj.type != RESPType::SIMPLE_STRING || obj.str != "OK") {
                    closeMaster();
                    return;
                }
                masterLink_.state = MasterSyncState::WaitingFullResync;
                continue;
            }

            if (masterLink_.state == MasterSyncState::WaitingFullResync) {
                if (obj.type != RESPType::SIMPLE_STRING || obj.str.rfind("FULLRESYNC ", 0) != 0) {
                    closeMaster();
                    return;
                }
                replication_.masterLinkUp = true;
                masterLink_.state = MasterSyncState::Streaming;
                continue;
            }

            std::vector<std::string> argv;
            std::string err;
            if (!CommandParser::toArgv(obj, argv, err)) {
                std::cerr << "replication command parse failed: " << err << "\n";
                closeMaster();
                return;
            }
            if (!dispatcher_.applyReplicationCommand(argv, err)) {
                std::cerr << "replication command apply failed: " << err << "\n";
                closeMaster();
                return;
            }
        }
    }
}

void EpollServer::closeMaster() {
    if (masterLink_.fd >= 0) {
        ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, masterLink_.fd, nullptr);
        ::close(masterLink_.fd);
        masterLink_.fd = -1;
    }
    replication_.masterLinkUp = false;
    masterLink_.parser = RESPParser();
    masterLink_.writeBuf.clear();
    masterLink_.state = MasterSyncState::Disconnected;
}

void EpollServer::removeReplica(int fd) {
    if (replicaFds_.erase(fd) > 0) {
        replication_.connectedReplicas = static_cast<int>(replicaFds_.size());
    }
}

void EpollServer::broadcastToReplicas(const std::vector<std::string>& argv) {
    if (replicaFds_.empty()) {
        return;
    }

    const std::string payload = RESPEncoder::array(argv);
    replication_.masterReplOffset += static_cast<long long>(payload.size());

    std::vector<int> fds(replicaFds_.begin(), replicaFds_.end());
    for (int fd : fds) {
        auto it = clients_.find(fd);
        if (it == clients_.end()) {
            removeReplica(fd);
            continue;
        }
        it->second.writeBuf += payload;
        if (!updateClientEvents(fd, true)) {
            closeClient(fd);
        }
    }
}

void EpollServer::run() {
    using Clock = std::chrono::steady_clock;

    epoll_event events[kMaxEvents];
    auto lastCron = Clock::now();

    for (;;) {
        const int n = ::epoll_wait(epollFd_, events, kMaxEvents, kEpollWaitTimeoutMs);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "epoll_wait failed: " << std::strerror(errno) << "\n";
            break;
        }

        for (int i = 0; i < n; ++i) {
            const int fd = events[i].data.fd;
            const uint32_t ev = events[i].events;

            if (fd == masterLink_.fd) {
                if ((ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                    closeMaster();
                    continue;
                }
                if ((ev & EPOLLIN) != 0) {
                    handleMasterRead();
                }
                if (masterLink_.fd >= 0 && (ev & EPOLLOUT) != 0) {
                    handleMasterWrite();
                }
                continue;
            }

            if (fd == listenFd_) {
                acceptClients();
                continue;
            }

            if ((ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                closeClient(fd);
                continue;
            }

            if ((ev & EPOLLIN) != 0) {
                handleClientRead(fd);
            }

            if (clients_.find(fd) != clients_.end() && (ev & EPOLLOUT) != 0) {
                handleClientWrite(fd);
            }
        }

        const auto now = Clock::now();
        const auto elapsedMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCron).count();
        if (elapsedMs >= kCronIntervalMs) {
            dispatcher_.cron();
            lastCron = now;
        }
    }
}
