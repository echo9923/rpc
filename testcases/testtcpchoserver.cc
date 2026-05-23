#include "mytinyrpc/net/netaddress.h"
#include "mytinyrpc/net/tcpserver.h"

int main() {
  tinyrpc::IPAddress addr("127.0.0.1", 19999);
  tinyrpc::TcpServer server(addr);

  if (!server.init()) {
    return 1;
  }

  server.start();
  return 0;
}
