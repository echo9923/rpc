#pragma once

#include "net/abstractcodec.h"
#include "net/abstractdispatcher.h"
#include "net/fdevent.h"
#include "net/iothreadpool.h"
#include "net/mutex.h"
#include "net/netaddress.h"
#include "net/reactor.h"
#include "net/socket.h"

// [第三方 API] google::protobuf::Service：Protobuf 编译器生成的服务基类，
// registerService() 的参数类型依赖此定义。
#include <google/protobuf/service.h>

#include <memory>
#include <unordered_map>

namespace tinyrpc {

class TcpConnection;

class TcpServer {
 public:
    using Ptr = std::shared_ptr<TcpServer>;

    explicit TcpServer(const IPAddress& addr,
                       AbstractCodec::Ptr codec = nullptr,
                       AbstractDispatcher::Ptr dispatcher = nullptr);
    ~TcpServer();

    const IPAddress& getLocalAddress() const;

    void setIOThreadNum(int ioThreadNum);
    int getIOThreadNum() const;
    std::size_t getConnectionCount() const;

    bool init();

    void start();

    // 注册一个 Protobuf Service 到分发器。
    // 内部转发给 TinyPbDispatcher::registerService()。
    // dispatcher 为 nullptr 或不是 TinyPbDispatcher 时返回 false。
    bool registerService(std::shared_ptr<google::protobuf::Service> service);

 private:
    void acceptLoop();

    void addConnection(Socket clientFd);
    void removeConnection(int fd);

 private:
    IPAddress m_addr;
    Socket m_listenFd {kInvalidSocket};

    Reactor m_reactor;
    FdEvent m_listenEvent;
    AbstractCodec::Ptr m_codec;
    AbstractDispatcher::Ptr m_dispatcher;
    std::unique_ptr<IOThreadPool> m_ioThreadPool;
    int m_ioThreadNum {0};
    std::unordered_map<int, std::shared_ptr<TcpConnection>> m_connections;
    mutable Mutex m_connectionMutex;
    bool m_running {false};
};

}
