#include "net/tcpconnection.h"
#include "comm/log.h"
#include "coroutine/coroutine.h"
#include "coroutine/coroutinehook.h"
#include "net/fdeventcontainer.h"
#include "net/http/httprequest.h"
#include "net/timer.h"
#include "net/tinypb/tinypbdata.h"

#include <cerrno>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace tinyrpc {

std::atomic<int> TcpConnection::s_aliveCount {0};

namespace {

std::string socketAddressToString(const sockaddr_in& addr)
{
    char ip[INET_ADDRSTRLEN] {};
    const char *rt = inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    if (rt == nullptr) {
        return "";
    }
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

std::string getSocketName(Socket fd)
{
    if (fd < 0) {
        return "";
    }

    sockaddr_in addr {};
    socklen_t len = sizeof(addr);
    // getsockname(2) 读取 socket 本端地址，参数依次为 fd、输出地址缓冲、缓冲长度指针。
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        return "";
    }
    return socketAddressToString(addr);
}

std::string getPeerName(Socket fd)
{
    if (fd < 0) {
        return "";
    }

    sockaddr_in addr {};
    socklen_t len = sizeof(addr);
    // getpeername(2) 读取 socket 对端地址，参数依次为 fd、输出地址缓冲、缓冲长度指针。
    if (getpeername(fd, reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        return "";
    }
    return socketAddressToString(addr);
}

}  // namespace

TcpConnection::TcpConnection(Socket fd, Reactor *reactor,
                             AbstractCodec::Ptr codec,
                             AbstractDispatcher::Ptr dispatcher)
    : m_fd(fd),
      m_reactor(reactor),
      m_codec(std::move(codec)),
      m_dispatcher(std::move(dispatcher))
{
    m_localAddrString = getSocketName(m_fd);
    m_peerAddrString = getPeerName(m_fd);
    refreshActiveTime();
    s_aliveCount.fetch_add(1);
}

TcpConnection::TcpConnection(Socket fd, Reactor *reactor, TcpConnectionType connectionType,
                             const IPAddress& peerAddr,
                             AbstractCodec::Ptr codec,
                             AbstractDispatcher::Ptr dispatcher)
    : m_fd(fd),
      m_reactor(reactor),
      m_connectionType(connectionType),
      m_peerAddr(peerAddr),
      m_codec(std::move(codec)),
      m_dispatcher(std::move(dispatcher))
{
    m_localAddrString = getSocketName(m_fd);
    m_peerAddrString = m_peerAddr.toString();
    refreshActiveTime();
    s_aliveCount.fetch_add(1);
}

TcpConnection::~TcpConnection()
{
    closeConnection();
    s_aliveCount.fetch_sub(1);
}

Socket TcpConnection::getFd() const
{
    return m_fd;
}

TcpConnectionType TcpConnection::getConnectionType() const
{
    return m_connectionType;
}

const IPAddress& TcpConnection::getPeerAddress() const
{
    return m_peerAddr;
}

std::string TcpConnection::getLocalAddressString() const
{
    if (!m_localAddrString.empty()) {
        return m_localAddrString;
    }
    return getSocketName(m_fd);
}

std::string TcpConnection::getPeerAddressString() const
{
    if (!m_peerAddrString.empty()) {
        return m_peerAddrString;
    }
    return getPeerName(m_fd);
}

AbstractCodec::Ptr TcpConnection::getCodec() const
{
    return m_codec;
}

TcpBuffer* TcpConnection::getInputBuffer()
{
    return &m_inputBuffer;
}

TcpBuffer* TcpConnection::getOutputBuffer()
{
    return &m_outputBuffer;
}

void TcpConnection::sendProtocolData(AbstractData *data)
{
    // 将协议数据对象通过当前 codec 编码后写入输出缓冲区。
    // 由后续的 output() 阶段通过 writeHook 发送到 socket。
    if (m_codec != nullptr && data != nullptr) {
        m_codec->encode(&m_outputBuffer, data);
    }
}

void TcpConnection::encodeClientRequest(TinyPbStruct *request)
{
    // 客户端连接只负责把业务请求编码进输出缓冲区；实际 socket 写出仍由 TcpClient 驱动。
    if (m_connectionType != TcpConnectionType::ClientConnection || m_codec == nullptr || request == nullptr) {
        return;
    }

    m_codec->encode(&m_outputBuffer, request);
}

void TcpConnection::appendClientInput(const char *data, size_t len)
{
    if (m_connectionType != TcpConnectionType::ClientConnection || data == nullptr || len == 0) {
        return;
    }

    m_inputBuffer.append(data, len);
    refreshActiveTime();
}

void TcpConnection::parseClientResponses()
{
    if (m_connectionType != TcpConnectionType::ClientConnection || m_codec == nullptr) {
        return;
    }

    while (m_inputBuffer.getReadableBytes() > 0) {
        TinyPbStruct response;
        m_codec->decode(&m_inputBuffer, &response);
        if (!response.m_decodeSucc) {
            break;
        }

        if (response.m_reqId.empty()) {
            ErrorLog("TcpConnection client received TinyPB response with empty reqId, peer = "
                     + m_peerAddr.toString());
            continue;
        }
        m_clientResponses[response.m_reqId] = response;
    }
}

bool TcpConnection::getClientResponse(const std::string& reqId, TinyPbStruct *response)
{
    if (response == nullptr) {
        return false;
    }

    auto it = m_clientResponses.find(reqId);
    if (it == m_clientResponses.end()) {
        return false;
    }

    *response = it->second;
    m_clientResponses.erase(it);
    return true;
}

bool TcpConnection::popClientResponse(TinyPbStruct *response)
{
    if (response == nullptr || m_clientResponses.empty()) {
        return false;
    }

    auto it = m_clientResponses.begin();
    *response = it->second;
    m_clientResponses.erase(it);
    return true;
}

size_t TcpConnection::getClientResponseCount() const
{
    return m_clientResponses.size();
}

void TcpConnection::startConnection()
{
    // 将 client fd 封装为 FdEvent 并注册到 Reactor。
    // 读和写都走协程 hook（readHook / writeHook），不设置任何 callback。
    if (m_reactor == nullptr) {
        ErrorLog("TcpConnection start failed, reactor is null, fd = " + std::to_string(m_fd));
        return;
    }

    m_fdEvent.setFd(m_fd);
    m_fdEvent.setReactor(m_reactor);

    if (!m_fdEvent.registerToReactor()) {
        ErrorLog("TcpConnection register fd event failed, fd = " + std::to_string(m_fd));
        return;
    }

    // 注册到 FdEventContainer，使透明 hook 可以按 fd 查找对应的 FdEvent。
    FdEventContainer::getInstance().registerFdEvent(&m_fdEvent);

    // 启动连接协程，读写均在此协程中串行完成。
    // 协程回调只捕获 weak_ptr，避免 TcpConnection -> Coroutine -> callback
    // -> shared_ptr<TcpConnection> 形成自引用环。协程内部关闭连接时，
    // closeWithCallback() 会把移除连接表动作延后到 Reactor task，保证当前
    // 协程完全返回前仍由 TcpServer::m_connections 保活。
    std::weak_ptr<TcpConnection> self = shared_from_this();
    m_readCoroutine = std::make_unique<Coroutine>([self]() {
        auto conn = self.lock();
        if (conn != nullptr) {
            conn->coroutineReadLoop();
        }
    });
    m_readCoroutine->resume();
}

void TcpConnection::sendData(const std::string& data)
{
    if (m_isClosed || data.empty()) {
        return;
    }

    // 仅追加到输出缓冲区，实际发送由 output() 完成
    m_outputBuffer.append(data);
}

void TcpConnection::closeConnection()
{
    if (m_isClosed || m_fd < 0) {
        return;
    }

    m_isClosed = true;

    InfoLog("TcpConnection close, fd = " + std::to_string(m_fd));

    // 先从容器移除，防止透明 hook 查到即将失效的 FdEvent
    FdEventContainer::getInstance().remove(m_fd);

    // 清除可能挂载在 FdEvent 上的协程指针，避免 Reactor 恢复已废弃的协程
    m_fdEvent.clearCoroutine();

    // 先删除事件再关闭 fd，避免 epoll 仍持有已关闭的 fd
    m_fdEvent.unregisterFromReactor();
    close(m_fd);
    m_fd = -1;
}

void TcpConnection::setCloseCallback(std::function<void(int)> cb)
{
    m_closeCallback = std::move(cb);
}

bool TcpConnection::isClosed() const
{
    return m_isClosed;
}

int64_t TcpConnection::getLastActiveTimeMs() const
{
    return m_lastActiveTimeMs;
}

void TcpConnection::refreshActiveTime()
{
    m_lastActiveTimeMs = getNowMs();
}

int TcpConnection::getAliveCountForTest()
{
    return s_aliveCount.load();
}

void TcpConnection::closeWithCallback()
{
    // 先保存一份 fd 副本，因为 closeConnection() 内部可能会修改/重置 m_fd，
    // 而后续回调需要使用关闭前的真实 fd 通知上层。
    Socket closedFd = m_fd;

    // 执行实际的连接关闭逻辑（如从 Reactor 移除事件、释放资源、置位 m_isClosed 等）。
    closeConnection();

    // 如果上层注册了关闭回调，则需要在关闭流程结束后触发它，
    // 以便通知调用方（例如连接管理器）该连接已断开。
    if (m_closeCallback) {
        // 使用局部变量保存回调，避免回调执行过程中对象被销毁导致悬空引用。
        auto callback = m_closeCallback;

        // 关键场景：当前正在执行的就是连接自身的读协程，
        // 如果直接同步调用 callback，可能会在回调中再次操作该协程（例如 resume/yield），
        // 从而引发"在协程内调度自身"的死锁或栈混乱问题。
        // 因此需要判断：当处于读协程上下文且 Reactor 可用时，
        // 将回调异步投递到 Reactor 的任务队列中，由 Reactor 在合适的时机（事件循环）执行，
        // 从而脱离当前协程上下文，保证安全。
        if (m_readCoroutine != nullptr
            && Coroutine::getCurrentCoroutine() == m_readCoroutine.get()
            && m_reactor != nullptr) {
            // 以任务形式投递给 Reactor，捕获 callback 和 closedFd 副本，
            // 保证异步执行时回调参数仍然有效。
            m_reactor->addTask([callback, closedFd]() {
                callback(closedFd);
            });
            return;
        }

        // 非读协程上下文（例如普通线程或其他协程触发关闭），
        // 可以直接同步调用回调，逻辑简单且无死锁风险。
        callback(closedFd);
    }
}

void TcpConnection::coroutineReadLoop()
{
    // 三段式主循环：input → execute → output
    // 当前 execute 保持 Echo 语义，后续接入 TinyPbCodec 时替换即可。
    while (!m_isClosed) {
        if (!input()) {
            break;
        }
        execute();
        output();
    }
}

bool TcpConnection::input()
{
    // 只负责从 socket 读取字节流并追加到 m_inputBuffer。
    // 返回 true 表示成功读到数据，false 表示连接需关闭。
    char buffer[1024];

    while (!m_isClosed) {
        // readHook 内部调用 ::read()，遇到 EAGAIN 时将当前协程挂到 m_fdEvent 上，
        // 通过 addListenEvent(EPOLLIN) + setCoroutineListenEvent(EPOLLIN) 注册可读事件，
        // 然后 Coroutine::yield() 让出 CPU。
        // Reactor 检测到 fd 可读且等待事件匹配后恢复协程，readHook 重试 ::read()。
        ssize_t n = readHook(&m_fdEvent, buffer, sizeof(buffer));

        if (n > 0) {
            m_inputBuffer.append(buffer, static_cast<size_t>(n));
            refreshActiveTime();
            return true;
        }

        if (n == 0) {
            // 对端关闭连接（TCP FIN），::read 返回 0
            InfoLog("coroutine read: client closed, fd = " + std::to_string(m_fd));
            closeWithCallback();
            return false;
        }

        // n < 0：发生错误，errno 由 readHook 设置
        if (errno == EINTR) {
            // 被信号中断，在 input() 内部重试，对 execute/output 透明
            continue;
        }

        ErrorLog("coroutine read error, fd = " + std::to_string(m_fd) + ", errno = " + std::to_string(errno));
        closeWithCallback();
        return false;
    }

    return false;
}

void TcpConnection::execute()
{
    // 消费 m_inputBuffer，将结果写入 m_outputBuffer。
    //
    // 无 codec 时保持 Echo 语义：将输入原样写入输出。
    // 有 codec 时循环调用 decode，处理粘包：
    //   - decode 成功：
    //     - 有 dispatcher：交给 dispatcher 处理，由 dispatcher 通过 sendProtocolData() 写回响应。
    //     - 无 dispatcher：encode 回写到输出（向后兼容的回环行为）。
    //   - decode 失败：半包或非法数据，不消费 buffer，等下一轮 input()。
    if (m_inputBuffer.getReadableBytes() == 0) {
        return;
    }

    if (m_codec == nullptr) {
        std::string data = m_inputBuffer.retrieveAllAsString();
        m_outputBuffer.append(data);
        return;
    }

    // 有 codec：循环 decode → dispatcher / encode，处理粘包
    while (m_inputBuffer.getReadableBytes() > 0) {
        std::unique_ptr<AbstractData> data = createProtocolData();
        if (data == nullptr) {
            ErrorLog("TcpConnection::execute unsupported protocol, fd = " + std::to_string(m_fd));
            closeWithCallback();
            break;
        }

        m_codec->decode(&m_inputBuffer, data.get());

        if (!data->m_decodeSucc) {
            // 半包或非法数据，不消费 buffer，等下一轮 input() 追加更多数据
            break;
        }

        if (m_dispatcher != nullptr) {
            // 有 dispatcher：交给分发器处理，由 dispatcher 内部调用 sendProtocolData() 写回响应
            m_dispatcher->dispatch(data.get(), this);
        } else {
            // 无 dispatcher：encode 回环（向后兼容）
            m_codec->encode(&m_outputBuffer, data.get());
        }
    }
}

std::unique_ptr<AbstractData> TcpConnection::createProtocolData() const
{
    if (m_codec == nullptr) {
        return nullptr;
    }

    // 根据 codec 的协议类型创建对应的协议数据对象。
    // TinyPB 与 HTTP 共用 TcpServer/TcpConnection 时，不能把解码对象写死为某一种协议。
    switch (m_codec->getProtocolType()) {
    case ProtocolType::TinyPb:
        return std::make_unique<TinyPbStruct>();
    case ProtocolType::Http:
        return std::make_unique<HttpRequest>();
    default:
        return nullptr;
    }
}

void TcpConnection::output()
{
    // 循环通过 writeHook 将输出缓冲区数据写入 socket。
    // writeHook 内部处理 EAGAIN：遇到发送缓冲区满时将协程挂到 FdEvent 上，
    // 通过 addListenEvent(EPOLLOUT) + setCoroutineListenEvent(EPOLLOUT) 等待可写，
    // Reactor 检测到 fd 可写且等待事件匹配后恢复协程，writeHook 重试 ::write()。
    while (!m_isClosed && m_outputBuffer.getReadableBytes() > 0) {
        ssize_t n = writeHook(
            &m_fdEvent,
            m_outputBuffer.getReadPtr(),
            m_outputBuffer.getReadableBytes()
        );

        if (n > 0) {
            // writeHook 写入 n 字节，推进输出缓冲区读指针
            m_outputBuffer.retrieve(static_cast<size_t>(n));
            continue;
        }

        // n == 0：write 返回 0 不应发生在 TCP socket 上，安全跳过重试
        if (n == 0) {
            continue;
        }

        // n < 0：发生错误，errno 由 writeHook 设置
        if (errno == EINTR) {
            // 被信号中断，重试 writeHook
            continue;
        }

        ErrorLog("TcpConnection::output write error, fd = " + std::to_string(m_fd) + ", errno = " + std::to_string(errno));
        closeWithCallback();
        break;
    }

    // 输出缓冲区已写空（或连接已关闭），删除 EPOLLOUT 避免 epoll 持续触发可写事件导致 CPU 空转。
    if (!m_isClosed && m_fdEvent.isRegistered()) {
        m_fdEvent.delListenEvent(EPOLLOUT);
        m_fdEvent.updateToReactor();
    }

    if (!m_isClosed && m_outputBuffer.getReadableBytes() == 0 && shouldCloseAfterOutput()) {
        closeWithCallback();
    }
}

bool TcpConnection::shouldCloseAfterOutput() const
{
    // 阶段 22 明确不实现 HTTP keep-alive；HTTP 响应写完后主动关闭连接。
    // TinyPB 服务端、TcpClient 客户端连接和无 codec 的 Echo 路径保持原有长连接语义。
    return m_connectionType == TcpConnectionType::ServerConnection
        && m_codec != nullptr
        && m_codec->getProtocolType() == ProtocolType::Http;
}

}
