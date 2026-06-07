#include "net/fdeventcontainer.h"
#include "net/reactor.h"

namespace tinyrpc {

FdEventContainer& FdEventContainer::getInstance()
{
    // thread_local 保证每个线程（IOThread）拥有独立实例，
    // 天然满足"不同 Reactor 中 fd 归属不串线"的隔离要求。
    static thread_local FdEventContainer instance;
    return instance;
}

bool FdEventContainer::registerFdEvent(FdEvent *event)
{
    if (event == nullptr) {
        return false;
    }
    int fd = event->getFd();
    if (fd < 0) {
        return false;
    }
    // try_emplace 在 key 不存在时才构造 Entry，已存在时不覆盖。
    auto [iter, inserted] = m_events.try_emplace(fd, Entry{event, nullptr});
    return inserted;
}

FdEvent* FdEventContainer::getOrCreate(int fd)
{
    if (fd < 0) {
        return nullptr;
    }
    auto iter = m_events.find(fd);
    if (iter != m_events.end()) {
        return iter->second.ptr;
    }
    // 惰性创建：绑定当前线程 Reactor，由容器 unique_ptr 拥有。
    // 透明 hook 拿到裸 fd 后通过此路径获取 FdEvent，
    // 后续的 readHook/writeHook 在遇到 EAGAIN 时按需注册到 epoll。
    auto owned = std::make_unique<FdEvent>(fd);
    Reactor *reactor = Reactor::getCurrentReactor();
    if (reactor != nullptr) {
        owned->setReactor(reactor);
    }
    FdEvent *raw = owned.get();
    m_events[fd] = Entry{raw, std::move(owned)};
    return raw;
}

FdEvent* FdEventContainer::get(int fd) const
{
    auto iter = m_events.find(fd);
    return (iter != m_events.end()) ? iter->second.ptr : nullptr;
}

void FdEventContainer::remove(int fd)
{
    // 无条件移除：无论条目是外部索引还是容器自有，一律 erase。
    // TcpConnection::closeConnection() 在 close(m_fd) 之前调用此方法，
    // 防止 fd 复用后 getOrCreate 返回过期的 FdEvent。
    m_events.erase(fd);
}

}  // namespace tinyrpc
