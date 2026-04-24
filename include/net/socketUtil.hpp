#pragma once

#include <string>

namespace SocketUtil {

bool setNonBlocking(int fd, std::string& err);

int createLoopbackListenSocket(int port, int backlog, std::string& err);

int createReplicaConnection(const std::string& host, int port, std::string& err);

} // namespace SocketUtil
