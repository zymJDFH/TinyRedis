#include "../../include/net/epollServer.hpp"

#include "../../include/command/commandParser.hpp"
#include "../../include/protocol/respEncoder.hpp"

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>

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
} // namespace

EpollServer::EpollServer(int port)
    : EpollServer(defaultConfigWithPort(port)) {}

EpollServer::EpollServer(ServerConfig config)
    : port_(config.port),
      listenFd_(-1),
      epollFd_(-1),
      config_(std::move(config)),
      metrics_(),
      dispatcher_(config_.appendOnly, config_.appendFilename, config_.appendFsync, &metrics_) {
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
    std::cout << "TinyRedis(epoll LT) listening on 127.0.0.1:" << port_
              << ", appendonly=" << (config_.appendOnly ? "yes" : "no")
              << ", appendfilename=" << config_.appendFilename
              << ", appendfsync=" << aofFsyncPolicyName(config_.appendFsync) << "\n";
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

            session.writeBuf += dispatcher_.dispatch(argv);
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
