# Reactor 任务队列与 swap 机制说明

## 背景代码

相关代码位于 `mytinyrpc/net/reactor.cc` 的 `Reactor::runPendingTasks()`：

```cpp
void Reactor::runPendingTasks()
{
    std::queue<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        tasks.swap(m_pendingTasks);
    }

    while (!tasks.empty()) {
        auto task = std::move(tasks.front());
        tasks.pop();
        if (task) {
            task();
        }
    }
}
```

任务入队逻辑位于 `Reactor::addTask()`：

```cpp
void Reactor::addTask(std::function<void()> task)
{
    if (!task) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        m_pendingTasks.push(std::move(task));
    }

    wakeup();
}
```

`m_pendingTasks` 是多个线程可能同时访问的共享任务队列，所以访问它时需要使用 `m_taskMutex` 保护。

## 疑问一：为什么要先定义本地队列再 swap？

原始疑问：

> 为什么要这样做？

代码：

```cpp
std::queue<std::function<void()>> tasks;
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    tasks.swap(m_pendingTasks);
}
```

这样做的核心目的，是把共享队列 `m_pendingTasks` 中的任务一次性转移到本地队列 `tasks`，然后立即释放锁。

交换完成后：

```text
tasks          = 原来 m_pendingTasks 里的所有任务
m_pendingTasks = 空队列
```

后续执行任务时：

```cpp
while (!tasks.empty()) {
    auto task = std::move(tasks.front());
    tasks.pop();
    if (task) {
        task();
    }
}
```

操作的是本地变量 `tasks`，不再访问共享队列 `m_pendingTasks`，因此不需要继续持有锁。

## 这样写的好处

### 1. 缩短加锁时间

锁真正需要保护的是共享队列 `m_pendingTasks`。

任务函数 `task()` 本身不需要 `m_taskMutex` 保护，而且任务执行时间不可控，可能很快，也可能很慢。

如果直接持锁执行任务，例如：

```cpp
std::lock_guard<std::mutex> lock(m_taskMutex);

while (!m_pendingTasks.empty()) {
    auto task = std::move(m_pendingTasks.front());
    m_pendingTasks.pop();

    if (task) {
        task();
    }
}
```

那么锁会一直持有到所有任务执行完成。

假设当前队列中有 3 个任务：

```text
task1 执行 20ms
task2 执行 50ms
task3 执行 10ms
```

那么 `m_taskMutex` 至少会被占用 80ms。其他线程在这段时间调用 `addTask()` 时，会卡在这里：

```cpp
std::lock_guard<std::mutex> lock(m_taskMutex);
```

当前写法只在 `swap` 时加锁：

```cpp
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    tasks.swap(m_pendingTasks);
}
```

锁只保护交换队列这一小段逻辑。任务执行过程在锁外完成，所以其他线程可以继续投递任务。

### 2. 避免死锁

如果持有 `m_taskMutex` 时执行 `task()`，而 `task()` 内部又调用了：

```cpp
reactor->addTask(...);
```

`addTask()` 又会尝试获取同一把锁：

```cpp
std::lock_guard<std::mutex> lock(m_taskMutex);
```

`std::mutex` 不是递归锁。同一个线程在已经持有锁的情况下再次获取同一把锁，会导致死锁。

当前写法在执行 `task()` 前已经释放了 `m_taskMutex`，所以任务内部再次调用 `addTask()` 不会因为这把锁而死锁。

### 3. 区分当前批次任务和新投递任务

`swap` 后，当前批次任务进入本地队列 `tasks`，共享队列 `m_pendingTasks` 变空。

如果执行当前批次时，其他线程又投递了新任务：

```text
tasks          = [A, B, C]  // 当前正在执行的批次
m_pendingTasks = [D]        // 新投递的任务
```

这样可以避免当前批次无限增长。

如果一个任务不断投递新任务，而 `runPendingTasks()` 一直处理同一个共享队列，可能导致当前这一轮一直处理不完。使用本地队列后，当前轮只处理已经取出的任务，新任务留到下一轮事件循环处理，调度边界更清楚。

### 4. 比逐个搬运任务更简洁

不用写成：

```cpp
while (!m_pendingTasks.empty()) {
    tasks.push(std::move(m_pendingTasks.front()));
    m_pendingTasks.pop();
}
```

而是直接：

```cpp
tasks.swap(m_pendingTasks);
```

语义更清楚：把待执行任务队列整体取走。

## 疑问二：为什么这样可以减少加锁时间？

原始疑问：

> 为什么这样可以减少加锁时间，swap 的原理是什么？

减少加锁时间的关键是：共享资源只有 `m_pendingTasks`，不是 `task()` 的执行过程。

当前写法的加锁范围：

```text
加锁
交换 tasks 和 m_pendingTasks
解锁
执行任务 A
执行任务 B
执行任务 C
```

如果持锁直接执行任务，加锁范围会变成：

```text
加锁
取任务 A
执行任务 A
取任务 B
执行任务 B
取任务 C
执行任务 C
解锁
```

区别在于，当前写法把锁保护范围从“整个任务执行过程”缩短成了“交换两个队列”。

也就是说：

```text
原来的潜在锁占用时间 = 所有任务执行时间之和
当前的锁占用时间     = swap 两个队列的时间
```

`task()` 执行时间不可控，而 `swap` 通常非常快，所以可以显著减少锁占用时间。

## swap 的原理

`std::queue` 是容器适配器，默认底层容器是 `std::deque`。

可以粗略理解成：

```cpp
template <typename T>
class queue {
private:
    std::deque<T> c;
};
```

执行：

```cpp
tasks.swap(m_pendingTasks);
```

本质上是交换两个 `queue` 内部的底层容器状态。

交换前：

```text
tasks:
  空队列

m_pendingTasks:
  [task1, task2, task3]
```

交换后：

```text
tasks:
  [task1, task2, task3]

m_pendingTasks:
  空队列
```

这个过程通常不是逐个移动或复制每一个 `std::function<void()>`，而是交换容器内部管理信息，例如内部存储块、指针、大小等。

可以粗略类比成交换两个指针：

```cpp
std::swap(tasks.internal_data, m_pendingTasks.internal_data);
```

真实实现比这个复杂，但核心思想是交换内部状态，而不是逐个处理元素。

因此，对 `std::queue` 默认底层 `std::deque` 来说，`swap` 通常是非常快的，适合放在锁里面执行。

## 疑问三：每个 STL 容器都有 swap 函数吗？

原始疑问：

> 每个 STL 容器都有 swap 函数吗？

标准库里的主流容器基本都有成员函数 `swap`。

常见顺序容器：

```cpp
std::vector<T>
std::deque<T>
std::list<T>
std::forward_list<T>
std::array<T, N>
```

常见关联容器：

```cpp
std::set<T>
std::map<K, V>
std::multiset<T>
std::multimap<K, V>
```

常见无序关联容器：

```cpp
std::unordered_set<T>
std::unordered_map<K, V>
std::unordered_multiset<T>
std::unordered_multimap<K, V>
```

容器适配器也有 `swap`：

```cpp
std::stack<T>
std::queue<T>
std::priority_queue<T>
```

所以当前代码中的：

```cpp
std::queue<std::function<void()>> tasks;
tasks.swap(m_pendingTasks);
```

是合法写法。

## 注意：不是所有 swap 都是 O(1)

大多数动态容器的 `swap` 通常是 O(1)，例如：

```cpp
std::vector<T>
std::deque<T>
std::list<T>
std::map<K, V>
std::unordered_map<K, V>
std::queue<T>
```

这些容器通常可以交换内部管理信息，而不需要逐个交换元素。

但是 `std::array<T, N>` 不一样。`std::array` 是固定大小数组，元素直接存放在对象内部，没有外部动态存储可以直接交换指针。

例如：

```cpp
std::array<int, 3> a = {1, 2, 3};
std::array<int, 3> b = {4, 5, 6};

a.swap(b);
```

`std::array::swap` 需要逐个交换元素，所以复杂度是 O(N)。

因此不能简单记成“所有 STL 容器的 swap 都是 O(1)”。

更准确的说法是：

```text
主流标准容器基本都有 swap
大多数动态容器 swap 很快，通常是 O(1)
std::array 的 swap 是 O(N)
容器适配器的 swap 复杂度取决于底层容器
```

## swap 的使用限制

两个容器要能 `swap`，通常要求类型一致。

可以：

```cpp
std::queue<int> a;
std::queue<int> b;

a.swap(b);
```

不可以：

```cpp
std::queue<int> a;
std::queue<long> b;

a.swap(b);  // 类型不同，不能交换
```

底层容器不同的 `queue` 也不能直接交换：

```cpp
std::queue<int, std::deque<int>> a;
std::queue<int, std::list<int>> b;

a.swap(b);  // 完整类型不同，不能交换
```

## 成员 swap 和 std::swap

可以使用成员函数：

```cpp
a.swap(b);
```

也可以使用：

```cpp
std::swap(a, b);
```

对标准容器来说，`std::swap` 通常会调用容器自己的高效交换逻辑。

在当前代码里：

```cpp
tasks.swap(m_pendingTasks);
```

语义更直接，表示把两个任务队列的内容交换。

## 总结

这段代码：

```cpp
std::queue<std::function<void()>> tasks;
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    tasks.swap(m_pendingTasks);
}
```

可以理解为：

```text
短暂加锁
把共享任务队列整体取走
立即释放锁
在锁外执行任务
```

它的主要价值是：

```text
1. 缩短 m_taskMutex 的持有时间
2. 避免 task() 内部再次 addTask() 导致死锁
3. 把当前批次任务和新投递任务分开
4. 利用 std::queue::swap 的高效交换特性，避免逐个搬运任务
```

所以在 Reactor 这种事件循环模型中，这是一种很常见、也很合理的任务队列处理方式。
