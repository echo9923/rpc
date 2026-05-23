#include "net/fdutil.h"
#include "comm/log.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>

namespace tinyrpc {

bool setNonBlock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    ErrorLog("fcntl F_GETFL failed, fd = " + std::to_string(fd));
    return false;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ErrorLog("fcntl F_SETFL O_NONBLOCK failed, fd = " + std::to_string(fd));
    return false;
  }

  return true;
}

bool setReuseAddr(int fd) {
  int value = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
    ErrorLog("setsockopt SO_REUSEADDR failed, fd = " + std::to_string(fd));
    return false;
  }

  return true;
}

}
