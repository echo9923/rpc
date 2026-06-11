#pragma once

#include "net/abstractcodec.h"
#include "net/abstractdispatcher.h"
#include "net/fdevent.h"
#include "net/iothreadpool.h"
#include "net/mutex.h"
#include "net/netaddress.h"
#include "net/reactor.h"
#include "net/socket.h"
#include "net/timer.h"

// [第三方 API] google::protobuf::Service：Protobuf 编译器生成的服务基类，
// registerService() 的参数类型依赖此定义。
#include <google/protobuf/service.h>

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

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
    void stop();
    bool isRunning() const;
    bool addTimerTask(const std::shared_ptr<TimerTask>& task);

    // 返回主 Reactor 指针，供测试等场景直接驱动事件循环
    Reactor* getReactor() { return &m_reactor; }

    // 注册一个 Protobuf Service 到分发器。
    // 内部转发给 TinyPbDispatcher::registerService()。
    // dispatcher 为 nullptr 或不是 TinyPbDispatcher 时返回 false。
    bool registerService(std::shared_ptr<google::protobuf::Service> service);

 private:
    void acceptLoop();

    void addConnection(Socket clientFd);
    void removeConnection(int fd);
    void shutdown();
    void closeListenSocket();
    void closeAllConnections();
    std::vector<std::shared_ptr<TcpConnection>> snapshotConnectionsAndClear();

 private:
     IPAddress m_addr;                                                          // 服务器监听地址（IP + 端口）
     Socket m_listenFd {kInvalidSocket};                                       // 监听套接字，用于 accept 客户端连接

     Reactor m_reactor;                                                        // 主 Reactor（IO 多路复用），负责监听 accept 事件
     FdEvent m_listenEvent;                                                    // 监听套接字对应的事件包装器，绑定到 m_reactor
     AbstractCodec::Ptr m_codec;                                               // 编解码器，用于解析/序列化网络数据（如 Protobuf）
     AbstractDispatcher::Ptr m_dispatcher;                                     // 请求分发器，将解码后的请求分发给对应的处理逻辑
     std::unique_ptr<IOThreadPool> m_ioThreadPool;                             // IO 线程池，每个线程拥有独立的子 Reactor，处理已建立的连接
     int m_ioThreadNum {0};                                                    // IO 线程池中线程的数量
     std::unordered_map<int, std::shared_ptr<TcpConnection>> m_connections;    // fd -> TcpConnection 映射表，管理所有活跃连接
     mutable Mutex m_connectionMutex;                                          // 保护 m_connections 的互斥锁（mutable 允许 const 方法加锁）
     std::atomic<bool> m_running {false};                                      // 服务器事件循环运行状态标志
     std::atomic<bool> m_shutdownStarted {false};                              // shutdown 是否已执行，保证 stop/析构幂等
};

}
