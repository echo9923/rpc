#include "net/tcpserver.h"
#include "net/tcpconnection.h"
#include "net/fdutil.h"
#include "net/tinypb/tinypbdispatcher.h"
#include "comm/log.h"

#include <cerrno>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>

namespace tinyrpc {

TcpServer::TcpServer(const IPAddress& addr,
                     AbstractCodec::Ptr codec,
                     AbstractDispatcher::Ptr dispatcher)
    : m_addr(addr),
      m_codec(std::move(codec)),
      m_dispatcher(std::move(dispatcher))
{
    DebugLog("TcpServer constructed on " + m_addr.toString());
}

TcpServer::~TcpServer()
{
    stop();
    shutdown();
}

const IPAddress& TcpServer::getLocalAddress() const
{
    return m_addr;
}

void TcpServer::setIOThreadNum(int ioThreadNum)
{
    if (ioThreadNum < 0) {
        ioThreadNum = 0;
    }
    m_ioThreadNum = ioThreadNum;
}

int TcpServer::getIOThreadNum() const
{
    return m_ioThreadNum;
}

std::size_t TcpServer::getConnectionCount() const
{
    MutexLockGuard lock(m_connectionMutex);
    return m_connections.size();
}

bool TcpServer::init()
{
    m_listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_listenFd < 0) {
        ErrorLog("create socket failed");
        return false;
    }

    if (!setReuseAddr(m_listenFd)) {
        return false;
    }

    if (!setNonBlock(m_listenFd)) {
        return false;
    }

    int rt = bind(m_listenFd, m_addr.getSockAddr(), m_addr.getSockLen());
    if (rt != 0) {
        ErrorLog("bind failed");
        return false;
    }

    // 第二个参数是监听队列的上限(backlog)，SOMAXCONN 表示交给系统使用默认的最大值
    rt = listen(m_listenFd, SOMAXCONN);
    if (rt != 0) {
        ErrorLog("listen failed");
        return false;
    }

    InfoLog("TcpServer listen on " + m_addr.toString());
    if (m_ioThreadNum > 0) {
        m_ioThreadPool = std::make_unique<IOThreadPool>(static_cast<std::size_t>(m_ioThreadNum));
    }
    return true;
}

void TcpServer::start()
{
    InfoLog("TcpServer start on " + m_addr.toString());

    m_shutdownStarted.store(false);

    // 将监听 fd 封装为 FdEvent，注册 EPOLLIN 事件到 Reactor。
    // 当有新的客户端连接到达时，Reactor 会触发 acceptLoop() 回调。
    m_listenEvent.setFd(m_listenFd);
    m_listenEvent.addListenEvent(EPOLLIN);
    m_listenEvent.setReadCallback([this]() { acceptLoop(); });

    if (!m_reactor.epollAdd(&m_listenEvent)) {
        ErrorLog("TcpServer add listen event to reactor failed");
        return;
    }

    m_running = true;
    while (m_running) {
        // waitOnce(-1) 表示无限等待，直到有事件发生或被信号中断。
        // 返回 -1 表示 epoll_wait 出错，此时退出事件循环。
        int rt = m_reactor.waitOnce(-1);
        if (rt < 0) {
            ErrorLog("TcpServer reactor waitOnce failed");
            break;
        }
    }

    shutdown();
}

void TcpServer::stop()
{
    m_running.store(false);
    closeAllConnections();
    m_reactor.stop();
}

bool TcpServer::isRunning() const
{
    return m_running.load();
}

bool TcpServer::addTimerTask(const std::shared_ptr<TimerTask>& task)
{
    if (task == nullptr || m_reactor.getTimer() == nullptr) {
        return false;
    }
    return m_reactor.getTimer()->addTimerTask(task);
}

// acceptLoop：接收新连接的主循环。
// 该函数由 Reactor 在监听套接字（m_listenFd）可读（即有新连接到达）时触发，
// 采用边缘触发（ET）或水平触发（LT）下的"循环 accept"策略，
// 直到内核连接队列被取空（返回 EAGAIN/EWOULDBLOCK）才退出，
// 这样能充分利用一次 epoll 通知处理尽可能多的连接，提高吞吐量。
void TcpServer::acceptLoop()
{
    // 进入 accept 循环前先做一次快速检查：若服务器已停止，则直接返回，避免无谓的 accept。
    if (!m_running.load()) {
        return;
    }

    // acceptLoop 由 Reactor 在监听 fd 可读时触发。
    // 循环 accept 直到连接队列清空（EAGAIN），充分利用一次事件通知。
    while (true) {
        // 每轮循环开始时再次检查运行标志，保证在停止时能够及时退出，避免"惊群"或忙等。
        if (!m_running.load()) {
            break;
        }

        // 用于保存新连接对端的 IPv4 地址信息（IP、端口等）。
        sockaddr_in clientAddr {};
        // accept 的第三个参数是"值-结果"类型：传入缓冲区大小，返回实际地址长度。
        socklen_t clientLen = sizeof(clientAddr);

        // 从监听套接字 m_listenFd 上取出一个已完成三次握手的连接，
        // 返回一个新的已连接套接字描述符 clientFd。
        // 由于 m_listenFd 已被设置为非阻塞模式，当没有新连接时 accept 会立即返回错误而非阻塞。
        Socket clientFd = accept(m_listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        // accept 失败：需要根据 errno 区分不同的错误情况分别处理。
        if (clientFd < 0) {
            // EINTR：accept 在阻塞过程中被信号中断，并非真正的错误，直接重试即可。
            if (errno == EINTR) {
                continue;
            }
            // EAGAIN/EWOULDBLOCK：当前非阻塞模式下，说明内核已完成连接队列已为空，
            // 本次 epoll 通知的所有连接都已处理完毕，退出循环，等待下一次 EPOLLIN 事件。
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            // 其他错误码：例如 EMFILE（达到进程文件描述符上限）、ENOMEM 等，
            // 记录错误日志并退出循环，避免在异常情况下无限重试占用 CPU。
            ErrorLog("accept failed, errno = " + std::to_string(errno));
            break;
        }

        // 成功接收一个新连接，打印日志便于调试与连接数统计。
        InfoLog("TcpServer accept client fd = " + std::to_string(clientFd));

        // 将新连接的套接字设置为非阻塞模式，
        // 以便后续在该 fd 上使用 epoll + 异步读写的 Reactor 模型，避免阻塞 IO 线程。
        if (!setNonBlock(clientFd)) {
            // 设置非阻塞失败：连接无法正常使用，记录错误并关闭该 fd，释放系统资源，继续处理下一个连接。
            ErrorLog("setNonBlock failed for client fd = " + std::to_string(clientFd));
            close(clientFd);
            continue;
        }

        // 套接字准备就绪，交给 addConnection 创建 TcpConnection 对象，
        // 并将其分发到合适的 IO 线程的 Reactor 上进行后续读写事件监听。
        addConnection(clientFd);
    }
}

void TcpServer::addConnection(Socket clientFd)
{
    if (!m_running.load()) {
        close(clientFd);
        return;
    }

    Reactor *connectionReactor = &m_reactor;
    IOThread *ioThread = nullptr;
    if (m_ioThreadPool != nullptr) {
        ioThread = m_ioThreadPool->getNextIOThread();
        if (ioThread != nullptr) {
            connectionReactor = ioThread->getReactor();
        }
    }

    auto conn = std::make_shared<TcpConnection>(clientFd, connectionReactor, m_codec, m_dispatcher);
    // 关闭回调携带连接的弱引用身份：removeConnection 据此校验连接表中的
    // 对象身份，防止 fd 复用后误删新连接的表项（详见 removeConnection 注释）。
    conn->setCloseCallback([this, weakConn = std::weak_ptr<TcpConnection>(conn)](int fd) {
        this->removeConnection(fd, weakConn);
    });

    {
        MutexLockGuard lock(m_connectionMutex);
        m_connections[clientFd] = conn;
    }

    if (ioThread != nullptr) {
        ioThread->addTask([conn]() {
            conn->startConnection();
        });
        return;
    }

    // 单线程模式保持旧语义：Main Reactor 负责连接读写。
    conn->startConnection();
}

void TcpServer::removeConnection(int fd, const std::weak_ptr<TcpConnection>& expected)
{
    MutexLockGuard lock(m_connectionMutex);
    auto it = m_connections.find(fd);
    if (it == m_connections.end()) {
        return;
    }

    // 身份校验：服务端主动关闭走"关闭 fd -> 延迟投递移除任务"的路径，
    // 在此期间内核可能把同一个 fd 分配给新 accept 的连接，连接表中的对象
    // 已经换成新连接。此时若按 fd 直接移除，会删掉新连接的表项，使新连接
    // 失去保活引用并在其协程仍在运行时被析构（协程 UAF 崩溃）。
    // expected 锁定失败说明原连接已随表项覆盖/移除而析构，当前表项必然
    // 属于其他连接，同样不能移除。
    auto expectedConn = expected.lock();
    if (expectedConn == nullptr || it->second.get() != expectedConn.get()) {
        return;
    }
    m_connections.erase(it);
}

void TcpServer::shutdown()
{
    bool expected = false;
    if (!m_shutdownStarted.compare_exchange_strong(expected, true)) {
        return;
    }

    m_running.store(false);
    closeListenSocket();

    // 先投递连接关闭任务，再停止 IO Reactor，避免任务没有执行机会。
    closeAllConnections();

    if (m_ioThreadPool != nullptr) {
        m_ioThreadPool->stop();
    }
}

void TcpServer::closeListenSocket()
{
    m_listenEvent.unregisterFromReactor();
    m_listenEvent.clearCoroutine();

    if (m_listenFd == kInvalidSocket) {
        return;
    }

    // close(2) 关闭监听 socket，参数是监听 fd；关闭后内核释放端口引用，
    // 后续服务端可重新 bind 同一地址。
    close(m_listenFd);
    m_listenFd = kInvalidSocket;
    m_listenEvent.setFd(kInvalidSocket);
}

void TcpServer::closeAllConnections()
{
    auto connections = snapshotConnectionsAndClear();
    for (auto& conn : connections) {
        if (conn != nullptr) {
            conn->closeConnection();
        }
    }
}

std::vector<std::shared_ptr<TcpConnection>> TcpServer::snapshotConnectionsAndClear()
{
    std::vector<std::shared_ptr<TcpConnection>> connections;
    MutexLockGuard lock(m_connectionMutex);
    connections.reserve(m_connections.size());
    for (auto& entry : m_connections) {
        connections.push_back(std::move(entry.second));
    }
    m_connections.clear();
    return connections;
}

bool TcpServer::registerService(std::shared_ptr<google::protobuf::Service> service)
{
    // dynamic_cast 将 AbstractDispatcher* 安全转为 TinyPbDispatcher*；
    // 若 m_dispatcher 实际类型不是 TinyPbDispatcher 则返回 nullptr，注册失败。
    auto *dispatcher = dynamic_cast<TinyPbDispatcher *>(m_dispatcher.get());
    if (dispatcher == nullptr) {
        ErrorLog("TcpServer::registerService failed: dispatcher is null or not TinyPbDispatcher");
        return false;
    }
    return dispatcher->registerService(std::move(service));
}

}
