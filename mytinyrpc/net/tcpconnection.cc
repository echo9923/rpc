#include "net/tcpconnection.h"
#include "comm/log.h"
#include "coroutine/coroutine.h"
#include "coroutine/coroutine_hook.h"

#include <cerrno>
#include <sys/epoll.h>
#include <unistd.h>

namespace tinyrpc {

TcpConnection::TcpConnection(Socket fd, Reactor *reactor)
    : m_fd(fd),
      m_reactor(reactor)
{
}

TcpConnection::~TcpConnection()
{
    closeConnection();
}

Socket TcpConnection::getFd() const
{
    return m_fd;
}

void TcpConnection::startConnection()
{
    // 将 client fd 封装为 FdEvent 并注册到 Reactor。
    // 读和写都走协程 hook（read_hook / write_hook），不设置任何 callback。
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

    // 启动连接协程，读写均在此协程中串行完成。
    // 协程回调持有 shared_ptr，防止协程执行期间 TcpConnection 被提前释放。
    // 例如对端关闭后 coroutineReadLoop 内调用 closeWithCallback 会触发
    // TcpServer::removeConnection 释放唯一的 shared_ptr，若无此引用则 this 悬空。
    auto self = shared_from_this();
    m_readCoroutine = std::make_unique<Coroutine>([self]() {
        self->coroutineReadLoop();
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

void TcpConnection::closeWithCallback()
{
    Socket closedFd = m_fd;
    closeConnection();

    if (m_closeCallback) {
        m_closeCallback(closedFd);
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
        // read_hook 内部调用 ::read()，遇到 EAGAIN 时将当前协程挂到 m_fdEvent 上，
        // 通过 addListenEvent(EPOLLIN) + setCoroutineListenEvent(EPOLLIN) 注册可读事件，
        // 然后 Coroutine::Yield() 让出 CPU。
        // Reactor 检测到 fd 可读且等待事件匹配后恢复协程，read_hook 重试 ::read()。
        ssize_t n = read_hook(&m_fdEvent, buffer, sizeof(buffer));

        if (n > 0) {
            m_inputBuffer.append(buffer, static_cast<size_t>(n));
            return true;
        }

        if (n == 0) {
            // 对端关闭连接（TCP FIN），::read 返回 0
            InfoLog("coroutine read: client closed, fd = " + std::to_string(m_fd));
            closeWithCallback();
            return false;
        }

        // n < 0：发生错误，errno 由 read_hook 设置
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
    // 当前阶段保持 Echo 语义：将输入原样写入输出。
    // 后续接入 TinyPbCodec 时替换此方法即可。
    if (m_inputBuffer.getReadableBytes() == 0) {
        return;
    }

    std::string data = m_inputBuffer.retrieveAllAsString();
    m_outputBuffer.append(data);
}

void TcpConnection::output()
{
    // 循环通过 write_hook 将输出缓冲区数据写入 socket。
    // write_hook 内部处理 EAGAIN：遇到发送缓冲区满时将协程挂到 FdEvent 上，
    // 通过 addListenEvent(EPOLLOUT) + setCoroutineListenEvent(EPOLLOUT) 等待可写，
    // Reactor 检测到 fd 可写且等待事件匹配后恢复协程，write_hook 重试 ::write()。
    while (!m_isClosed && m_outputBuffer.getReadableBytes() > 0) {
        ssize_t n = write_hook(
            &m_fdEvent,
            m_outputBuffer.getReadPtr(),
            m_outputBuffer.getReadableBytes()
        );

        if (n > 0) {
            // write_hook 写入 n 字节，推进输出缓冲区读指针
            m_outputBuffer.retrieve(static_cast<size_t>(n));
            continue;
        }

        // n == 0：write 返回 0 不应发生在 TCP socket 上，安全跳过重试
        if (n == 0) {
            continue;
        }

        // n < 0：发生错误，errno 由 write_hook 设置
        if (errno == EINTR) {
            // 被信号中断，重试 write_hook
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
}

}
