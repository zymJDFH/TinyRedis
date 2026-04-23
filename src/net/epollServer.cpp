#include "../../include/net/epollServer.hpp"

#include "../../include/command/commandParser.hpp"
#include "../../include/protocol/respEncoder.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
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
      masterFd_(-1),
      config_(std::move(config)),
      metrics_(),
      replication_(config_.replication),
      dispatcher_(config_.appendOnly, config_.appendFilename, config_.appendFsync, &metrics_, &replication_),
      replicaFds_(),
      masterParser_(),
      masterWriteBuf_(),
      masterSyncState_(MasterSyncState::Disconnected) {
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
    if (masterFd_ >= 0) {
        ::close(masterFd_);
        masterFd_ = -1;
    }
}

bool EpollServer::setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool EpollServer::initListenSocket() {
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    if (::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt failed: " << std::strerror(errno) << "\n";
        return false;
    }

    if (!setNonBlocking(listenFd_)) {
        std::cerr << "setNonBlocking listen fd failed: " << std::strerror(errno) << "\n";
        return false;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind 127.0.0.1:" << port_ << " failed: " << std::strerror(errno) << "\n";
        return false;
    }

    if (::listen(listenFd_, kBacklog) < 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << "\n";
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

        if (!setNonBlocking(fd)) {
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
    masterFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (masterFd_ < 0) {
        std::cerr << "replication socket failed: " << std::strerror(errno) << "\n";
        return false;
    }

    if (!setNonBlocking(masterFd_)) {
        std::cerr << "replication setNonBlocking failed: " << std::strerror(errno) << "\n";
        closeMaster();
        return false;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(replication_.masterPort));
    if (::inet_pton(AF_INET, replication_.masterHost.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "replication master host must be an IPv4 address: " << replication_.masterHost << "\n";
        closeMaster();
        return false;
    }

    const int rc = ::connect(masterFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS) {
        std::cerr << "connect master " << replication_.masterHost << ":" << replication_.masterPort
                  << " failed: " << std::strerror(errno) << "\n";
        closeMaster();
        return false;
    }

    masterWriteBuf_ += RESPEncoder::array({"PING"});
    masterWriteBuf_ += RESPEncoder::array({"REPLCONF", "listening-port", std::to_string(port_)});
    masterWriteBuf_ += RESPEncoder::array({"PSYNC", "?", "-1"});
    masterSyncState_ = MasterSyncState::WaitingPong;

    epoll_event ev {};
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP;
    ev.data.fd = masterFd_;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, masterFd_, &ev) < 0) {
        std::cerr << "epoll_ctl add master fd failed: " << std::strerror(errno) << "\n";
        closeMaster();
        return false;
    }

    return true;
}

void EpollServer::handleMasterWrite() {
    while (!masterWriteBuf_.empty()) {
        const ssize_t n = ::send(masterFd_, masterWriteBuf_.data(), masterWriteBuf_.size(), 0);
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
        masterWriteBuf_.erase(0, static_cast<size_t>(n));
    }

    if (masterWriteBuf_.empty()) {
        epoll_event ev {};
        ev.events = EPOLLIN | EPOLLRDHUP;
        ev.data.fd = masterFd_;
        if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, masterFd_, &ev) < 0) {
            closeMaster();
        }
    }
}

void EpollServer::handleMasterRead() {
    char buf[kReadBufSize];

    for (;;) {
        const ssize_t n = ::recv(masterFd_, buf, sizeof(buf), 0);
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

        masterParser_.feed(buf, static_cast<size_t>(n));

        for (;;) {
            RESPObject obj;
            bool ok = false;
            try {
                ok = masterParser_.parse(obj);
            } catch (const std::exception& ex) {
                std::cerr << "replication protocol error: " << ex.what() << "\n";
                closeMaster();
                return;
            }
            if (!ok) {
                break;
            }

            if (masterSyncState_ == MasterSyncState::WaitingPong) {
                if (obj.type != RESPType::SIMPLE_STRING || obj.str != "PONG") {
                    closeMaster();
                    return;
                }
                masterSyncState_ = MasterSyncState::WaitingReplconf;
                continue;
            }

            if (masterSyncState_ == MasterSyncState::WaitingReplconf) {
                if (obj.type != RESPType::SIMPLE_STRING || obj.str != "OK") {
                    closeMaster();
                    return;
                }
                masterSyncState_ = MasterSyncState::WaitingFullResync;
                continue;
            }

            if (masterSyncState_ == MasterSyncState::WaitingFullResync) {
                if (obj.type != RESPType::SIMPLE_STRING || obj.str.rfind("FULLRESYNC ", 0) != 0) {
                    closeMaster();
                    return;
                }
                replication_.masterLinkUp = true;
                masterSyncState_ = MasterSyncState::Streaming;
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
    if (masterFd_ >= 0) {
        ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, masterFd_, nullptr);
        ::close(masterFd_);
        masterFd_ = -1;
    }
    replication_.masterLinkUp = false;
    masterSyncState_ = MasterSyncState::Disconnected;
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

            if (fd == masterFd_) {
                if ((ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                    closeMaster();
                    continue;
                }
                if ((ev & EPOLLIN) != 0) {
                    handleMasterRead();
                }
                if (masterFd_ >= 0 && (ev & EPOLLOUT) != 0) {
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
