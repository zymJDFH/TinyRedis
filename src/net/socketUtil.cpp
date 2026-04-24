#include "../../include/net/socketUtil.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
bool setReuseAddr(int fd, std::string& err) {
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
        err = std::strerror(errno);
        return false;
    }
    return true;
}
} // namespace

namespace SocketUtil {

bool setNonBlocking(int fd, std::string& err) {
    err.clear();

    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        err = std::strerror(errno);
        return false;
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        err = std::strerror(errno);
        return false;
    }
    return true;
}

int createLoopbackListenSocket(int port, int backlog, std::string& err) {
    err.clear();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::strerror(errno);
        return -1;
    }

    if (!setReuseAddr(fd, err) || !setNonBlocking(fd, err)) {
        (void)::close(fd);
        return -1;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        err = std::strerror(errno);
        (void)::close(fd);
        return -1;
    }

    if (::listen(fd, backlog) != 0) {
        err = std::strerror(errno);
        (void)::close(fd);
        return -1;
    }

    return fd;
}

int createReplicaConnection(const std::string& host, int port, std::string& err) {
    err.clear();

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::strerror(errno);
        return -1;
    }

    if (!setNonBlocking(fd, err)) {
        (void)::close(fd);
        return -1;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        err = "master host must be an IPv4 address";
        (void)::close(fd);
        return -1;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 && errno != EINPROGRESS) {
        err = std::strerror(errno);
        (void)::close(fd);
        return -1;
    }

    return fd;
}

} // namespace SocketUtil
