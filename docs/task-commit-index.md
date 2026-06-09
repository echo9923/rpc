# 任务提交索引

本文记录任务号与完成提交之间的对应关系，主要用于让 AI 助手在用户说“回退到任务多少”时快速定位 Git 提交。

AI 处理任务号相关请求时，应先遵守仓库根目录 `AGENTS.md` 中的“任务回退与索引规则”，再使用本文提供的详细映射。

## 使用规则

1. “回退到任务 N”默认理解为：定位到“任务 N 完成提交”的状态。
2. “回退到任务 N 开始前”理解为：定位到“任务 N 完成提交”的父提交，也就是 `<任务N提交>^`。
3. “撤销任务 N”不是回退到某个历史状态，而是对该任务提交执行 `git revert <任务N提交>`。如果后续任务依赖该任务，必须先评估冲突和行为影响。
4. 执行任何破坏性回退前，必须先查看 `git status --short`，并得到用户明确允许。
5. 本文件中的短提交号用于快速定位；实际操作前可以用 `git rev-parse <短提交号>` 展开为完整提交号。

推荐先只做定位和核对：

```bash
git show --stat <任务完成提交>
git log --oneline --decorate <任务完成提交>^..<任务完成提交>
```

只有用户明确要求修改当前分支历史时，才考虑 `git reset --hard <任务完成提交>` 等破坏性命令。

## 持久访问方式

本文件作为普通工作区文件时，会随着 `git checkout` 或 `git reset --hard <旧提交>` 回到旧快照。如果旧提交还没有本文件，工作区里的 `docs/task-commit-index.md` 会消失。

因此，长期使用时不要只依赖当前工作区文件，而应优先使用 Git 标签：

```bash
git show task-index:docs/task-commit-index.md
git tag --list "task-*"
git show --stat task-38
```

其中：

1. `task-index` 标签指向包含本索引文档的提交。即使当前工作区回退到旧提交，也可以通过 `git show task-index:docs/task-commit-index.md` 读取本表。
2. `task-08`、`task-09`、`task-10` 这类标签直接指向对应任务完成后的提交。
3. 如果用户明确要求“回退到任务三十八”，优先定位 `task-38`。执行破坏性回退前仍必须先征得用户确认。

## 明确任务提交索引

以下任务号来自提交标题中的明确任务记录，可信度最高。若一个提交同时完成多个任务，则每个任务单独占一行，指向同一个提交。

| 任务 | 完成提交 | 提交标题 | 主要内容 | 备注 |
|---|---|---|---|---|
| 任务十六 | `5521c67` | `feat(tcpconnection ):完成任务16 支持 EPOLLOUT 事件驱动写入` | 支持 `EPOLLOUT` 事件驱动写入 | 标题明确 |
| 任务十七 | `f7813a7` | `feat(tcpbuffer): 任务十七已完成 添加 TcpBuffer 并接入 TcpConnection 输出缓冲` | 添加 `TcpBuffer`，接入输出缓冲 | 标题明确 |
| 任务十八 | `9edf4b5` | `feat(tcpconnection): 任务十八已完成 接入 TcpConnection 输入缓冲区` | 接入 `TcpConnection` 输入缓冲区 | 标题明确 |
| 任务十九 | `2450b99` | `feat(fdevent): 任务十九已完成 对齐 FdEvent 与 Reactor 职责边界` | 对齐 `FdEvent` 与 `Reactor` 职责边界 | 标题明确 |
| 任务二十 | `6432771` | `feat(coroutine):任务二十完成 实现最小协程对象及其上下文切换功能，添加相关测试用例` | 实现最小协程对象和上下文切换 | 标题明确；其后有非任务提交 `4159bb1` |
| 任务二十一 | `8218610` | `feat(coroutine): 任务二十一已完成 引入 read_hook/write_hook 的最小雏形` | 引入 `read_hook` / `write_hook` 雏形 | 标题明确 |
| 任务二十二 | `6ef059a` | `feat(reactor): 任务二十二已完成 让 Reactor 恢复挂载在 FdEvent 上的协程` | `Reactor` 恢复挂载在 `FdEvent` 上的协程 | 标题明确 |
| 任务二十三 | `0f6491b` | `feat(tcpconnection): 任务二十三已完成 将读路径从 EPOLLIN callback 迁移至协程 read_hook` | 读路径迁移到协程 `read_hook` | 标题明确 |
| 任务二十四 | `2099ab3` | `feat(tcpconnection): 任务二十四已完成 将写路径从 EPOLLOUT callback 迁移至协程 write_hook` | 写路径迁移到协程 `write_hook` | 标题明确 |
| 任务二十五 | `1db1452` | `feat(codec): 任务二十五已完成 新增 AbstractCodec 测试用例并更新文档` | 新增 `AbstractCodec` 测试并更新文档 | 标题明确 |
| 任务二十六 | `346c667` | `feat(tinypb): 完成任务二十六，定义 TinyPB 协议数据结构并新增测试用例` | 定义 TinyPB 协议数据结构 | 标题明确 |
| 任务二十七 | `fe029ea` | `feat(tinypb): 任务二十七已完成 实现 TinyPB 编码器的最小 encode 路径` | 实现 TinyPB 最小 encode 路径 | 标题明确 |
| 任务二十八 | `a2e10c8` | `feat(tinypb): 任务二十八已完成 实现 TinyPB 解码器的完整单包 decode 路径` | 实现 TinyPB 单包 decode 路径 | 标题明确 |
| 任务二十九 | `1c646ca` | `feat(tinypb): 任务二十九已完成 增强 TinyPB decode 的流式拆包边界处理` | 增强 TinyPB 流式拆包边界处理 | 标题明确 |
| 任务三十 | `57d7b1a` | `feat(tinypb): 任务三十已完成 补充 TinyPB decode 的错误包恢复和包长安全校验` | 补充错误包恢复和包长安全校验 | 标题明确 |
| 任务三十一 | `2e9e8ac` | `refactor(reactor): 任务三十一已完成 统一 Reactor 方法命名并重构 TcpConnection 读写流程为三段式` | 统一 `Reactor` 方法命名，读写流程三段式 | 标题明确 |
| 任务三十二 | `f821a88` | `feat(tcpconnection): 任务三十二已完成 为 TcpConnection 接入 AbstractCodec 并实现 TinyPB 最小协议回环` | `TcpConnection` 接入 `AbstractCodec` | 标题明确 |
| 任务三十三 | `d700e31` | `feat(dispatcher): 任务三十三已完成 引入 AbstractDispatcher 并让 TcpConnection 通过 TinyPB Dispatcher 生成响应` | 引入 `AbstractDispatcher`，接入 TinyPB Dispatcher | 标题明确 |
| 任务三十四 | `660bcd7` | `feat(rpc): 任务三十五与三十四已完成 接入 Protobuf Service 并打通服务端 RPC 全链路` | 接入 Protobuf Service | 与任务三十五同提交 |
| 任务三十五 | `660bcd7` | `feat(rpc): 任务三十五与三十四已完成 接入 Protobuf Service 并打通服务端 RPC 全链路` | 打通服务端 RPC 全链路 | 与任务三十四同提交 |
| 任务三十六 | `d260449` | `feat(tcpclient): 任务三十六已完成 新增 TcpClient 及测试用例并完善构建依赖检查` | 新增 `TcpClient` 和测试 | 标题明确 |
| 任务三十七 | `40b04d1` | `feat(tcpclient): 任务三十七已完成 让 TcpClient 完成最小 TinyPB 同步请求/响应闭环` | `TcpClient` 完成最小 TinyPB 同步闭环 | 标题明确；其后有非任务提交 `10271c0` |
| 任务三十八 | `f8dbe27` | `feat(tinypb): 任务三十八已完成 实现最小 TinyPbRpcChannel` | 实现最小 `TinyPbRpcChannel` | 标题明确 |
| 任务三十九 | `9719e4b` | `feat(tinypb): 任务三十九已完成 打通 Stub 到服务端同步 RPC` | 打通 Stub 到服务端同步 RPC | 标题明确 |
| 任务四十 | `1eb2bdb` | `feat(tinypb): 任务四十已完成 补齐请求号与控制器语义` | 补齐请求号和 controller 语义 | 标题明确 |
| 任务四十一 | `1e1837b` | `feat(tcpclient): 任务四十一已完成 增加同步超时与失败路径` | 增加同步超时和失败路径 | 标题明确 |
| 任务四十二 | `51fef24` | `docs(rpc): 任务四十二已完成 补齐阶段八回归脚本与调用链文档` | 补齐阶段八脚本和调用链文档 | 标题明确 |
| 任务四十三 | `5585e59` | `feat(tcpclient): 任务四十三已完成 明确重连和关闭边界` | 明确 `TcpClient` 重连和关闭边界 | 标题明确 |
| 任务四十四 | `d97779d` | `feat(tcpclient): 任务四十四已完成 补齐同步客户端错误码矩阵` | 补齐同步客户端错误码矩阵 | 标题明确 |
| 任务四十五 | `9a4e55c` | `test(rpc): 任务四十五已完成 增加同步 RPC 稳定性回归脚本` | 增加同步 RPC 稳定性回归脚本 | 标题明确 |
| 任务四十六 | `102de69` | `docs(rpc): 任务四十六已完成 明确同步响应缓存边界` | 明确同步响应缓存边界 | 标题明确 |
| 任务四十七 | `eacc761` | `feat(timer): 任务四十七已完成 增加 TimerEvent 基础能力` | 增加 `TimerEvent` 基础能力 | 标题明确 |
| 任务四十八 | `ffff111` | `feat(timer): 任务四十八已完成 接入 timerfd 到 Reactor` | 接入 `timerfd` 到 `Reactor` | 标题明确 |
| 任务四十九 | `db8f46a` | `feat(reactor): 任务四十九已完成 增加任务队列和 wakeup fd` | 增加任务队列和 wakeup fd | 标题明确 |
| 任务五十 | `e5fcd69` | `feat(reactor): 任务五十已完成 明确事件生命周期` | 明确事件生命周期 | 标题明确 |
| 任务五十一 | `359c7a7` | `feat(tcp): 任务五十一已完成 增加连接空闲超时` | 增加连接空闲超时 | 标题明确 |
| 任务五十二 | `e4202a8` | `docs(reactor): 任务五十二已完成 补齐阶段十调试文档` | 补齐阶段十调试文档 | 标题明确 |
| 任务五十三 | `069c0ec` | `feat(thread): 任务五十三已完成 增加基础锁工具` | 增加基础锁工具 | 标题明确 |
| 任务五十四 | `b8298c0` | `feat(iothread): 任务五十四已完成 增加 IOThread 生命周期` | 增加 `IOThread` 生命周期 | 标题明确 |
| 任务五十五 | `d294e3e` | `feat(iothread): 任务五十五已完成 增加 IOThreadPool` | 增加 `IOThreadPool` | 标题明确 |
| 任务五十六 | `7285132` | `feat(tcpserver): 任务五十六已完成 接入 IOThreadPool` | `TcpServer` 接入 `IOThreadPool` | 标题明确 |
| 任务五十七 | `1d40f17` | `docs(tcp): 任务五十七已完成 明确连接所有权和状态机` | 明确连接所有权和状态机 | 标题明确 |
| 任务五十八 | `88cdcf8` | `feat(http): 任务五十八已完成 增加基础数据结构` | 增加 HTTP 基础数据结构 | 标题明确 |
| 任务五十九 | `82804a7` | `feat(http): 任务五十九已完成 增加请求解码` | 增加 HTTP 请求解码 | 标题明确 |
| 任务六十 | `24e4712` | `feat(http): 任务六十已完成 增加响应编码` | 增加 HTTP 响应编码 | 标题明确 |
| 任务六十一 | `9fafdaf` | `feat(http): 任务六十一已完成 增加路径分发` | 增加 HTTP 路径分发 | 标题明确 |
| 任务六十二 | `9f9af43` | `feat(http): 任务六十二已完成 接入 HTTP Server` | 接入 HTTP Server | 标题明确 |
| 任务六十三 | `6cd6a4e` | `feat(config): 任务六十三已完成 增加默认配置` | 增加默认配置 | 标题明确 |
| 任务六十四 | `ff52db5` | `feat(config): 任务六十四已完成 支持 XML 配置读取` | 支持 XML 配置读取 | 标题明确 |
| 任务六十五 | `6f743b2` | `feat(log): 任务六十五已完成 增加文件日志` | 增加文件日志 | 标题明确 |
| 任务六十六 | `10928ae` | `feat(start): 任务六十六已完成 增加启动入口` | 增加启动入口 | 标题明确 |
| 任务六十七 | `188910b` | `feat(runtime): 任务六十七已完成 增加请求上下文` | 增加请求上下文 | 标题明确 |
| 任务六十八 | `8166e1a` | `docs(coroutine): 任务六十八已完成 梳理协程模型` | 梳理协程模型 | 标题明确 |
| 任务六十九 | `8c13c9b` | `feat(coroutine): 任务六十九已完成 增加 connect hook` | 增加 `connect_hook` | 标题明确 |
| 任务九十六 | `0d8729b` | `完成任务九十六：TcpClient 接入客户端 TcpConnection` | `TcpClient` 接入客户端 `TcpConnection` 编码、缓冲区和响应缓存 | 标题明确；阶段 20 |
| 任务九十七 | `67d5052` | `完成任务九十七：同步 TcpClient 改为 Reactor Timer 超时模型` | 同步 `TcpClient` connect / write / read 改为 Reactor + Timer 超时模型 | 标题明确；阶段 20 |
| 任务九十八 | `83baf6f` | `完成任务九十八：客户端响应缓存和迟到响应处理` | 按 reqId 等待匹配响应，丢弃未知或错误 reqId 响应 | 标题明确；阶段 20 |
| 任务九十九 | `af2b29b` | `完成任务九十九：连接复用关闭和失败重建策略` | 增加连接复用开关、显式关闭和失败后重建策略 | 标题明确；阶段 20 |
| 任务一百 | `c011f92` | `完成任务一百：客户端 Reactor 化回归脚本收口` | 新增客户端 Reactor 化回归脚本并接入总回归，更新阶段文档 | 标题明确；阶段 20 |
| 任务一百零一 | `0cce07d` | `完成任务一百零一：抽出长生命周期 AsyncClientSession` | 新增 `AsyncClientSession` 并让异步 Channel 持有长生命周期 session | 标题明确；阶段 21 |
| 任务一百零二 | `ceec739` | `完成任务一百零二：添加 Reactor 写事件基础设施` | 增加异步 session 的 flushOutput、FdEvent 和 EPOLLOUT 基础设施 | 标题明确；阶段 21 |
| 任务一百零三 | `3874118` | `完成任务一百零三：异步读取循环基础设施和非阻塞 socket` | 接入 non-blocking socket、EPOLLIN 读取循环和 read callback | 标题明确；阶段 21 |
| 任务一百零四 | `2feb9e6` | `完成任务一百零四：timeout/cancel 打断网络状态` | 补齐 timeout、cancel、stop 对 pending 的一次性仲裁和失败路径 | 标题明确；阶段 21 |
| 任务一百零五 | `2c8d0f9` | `完成任务一百零五：真实 TcpServer 异步 RPC 端到端验收` | 接入真实 TcpServer 异步 E2E 验收和异步回归脚本增强 | 标题明确；阶段 21；其后有阶段收口提交 `230b334` |

## 早期任务推定索引

以下任务号来自 `docs/stage-2.md` 的阶段记录，但对应提交标题没有全部显式写出任务号。用于快速定位时可参考；真正执行回退前应再用 `git show --stat <提交>` 核对文件变化。

| 任务 | 推定完成提交 | 提交标题 | 推定依据 | 备注 |
|---|---|---|---|---|
| 任务八 | `c13deb9` | `feat: 添加阶段 2 非阻塞 IO 处理与 epoll accept 示例` | `docs/stage-2.md` 记录任务八为 listen fd 非阻塞；该提交新增 `fdutil` 并修改 `TcpServer` | 与任务九、任务十同提交 |
| 任务九 | `c13deb9` | `feat: 添加阶段 2 非阻塞 IO 处理与 epoll accept 示例` | `docs/stage-2.md` 记录任务九为 client fd 非阻塞和 `EAGAIN` 处理；该提交修改 `TcpConnection` | 与任务八、任务十同提交 |
| 任务十 | `c13deb9` | `feat: 添加阶段 2 非阻塞 IO 处理与 epoll accept 示例` | `docs/stage-2.md` 记录任务十为最小 epoll；该提交新增 `test_epoll_accept.cc` | 与任务八、任务九同提交 |
| 任务十一 | `5e31400` | `feat: 添加 FdEvent 事件封装与测试用例` | 提交标题对应 `FdEvent` 抽象 | 推定 |
| 任务十二 | `34eefdf` | `feat: 添加 epoll Reactor 事件循环与测试用例` | 提交标题对应 `Reactor` 抽象 | 推定 |
| 任务十三 | `47c116f` | `feat: 添加 test_reactor_accept 测试用例，演示 Reactor 模式下的连接接受逻辑` | 提交标题对应 Reactor accept 测试 | 推定 |
| 任务十四 | `62ce500` | `feat(tcpserver): 接入 Reactor 事件循环处理连接接收` | 提交标题对应 `TcpServer` 接入 Reactor | 推定 |
| 任务十五 | `6fc0bb5` | `feat(tcpserver): 将客户端连接接入 Reactor 事件驱动处理` | 阶段文档记录任务十五为连接关闭回调；该提交推进连接接入 Reactor 驱动处理 | 推定，建议回退前重点核对 |

`docs/stage-2.md` 记录了任务八到任务十九的阶段顺序。仓库当前没有可靠材料把任务一到任务七映射到单独提交；如用户要求回退到任务一到任务七，应先人工核对早期提交和旧计划，再决定目标提交。

## 非任务提交

这些提交不直接对应某个任务完成记录，但位于任务之间。若用户说“任务 N 完成后的提交”应使用任务表中的提交；若用户说“任务 N 之后、任务 N+1 之前的最新状态”，需要考虑这些提交。

| 提交 | 位置 | 提交标题 | 说明 |
|---|---|---|---|
| `4159bb1` | 任务二十之后，任务二十一之前 | `补充 Linux 构建说明和协程汇编注释` | 文档和汇编注释补充 |
| `10271c0` | 任务三十七之后，任务三十八之前 | `docs(plan): 删除未来任务计划文档并新增分支管理规范` | 计划文档和分支管理规范调整 |
| `230b334` | 任务一百零五之后，任务一百零六之前 | `完成阶段二十一异步 RPC 网络路径收口` | 将异步 Channel 默认路径收口为 EPOLLIN/EPOLLOUT 驱动并更新阶段文档 |

## 维护规则

1. 未来每完成一个任务并提交后，都应在“明确任务提交索引”中追加一行。
2. 提交标题建议统一包含 `任务X已完成`，例如 `feat(module): 任务七十已完成 增加 xxx`。
3. 一个提交完成多个任务时，应拆成多行记录，备注写明“与任务X同提交”。
4. 任务间的非任务提交应追加到“非任务提交”表，避免以后回退语义混淆。
5. 追加新任务后，应为新任务创建对应标签，例如 `git tag task-70 <任务七十完成提交>`。
6. 如果本索引文档被更新，应把 `task-index` 标签移动到最新索引提交。
7. 更新索引前建议执行：

```bash
git log --oneline --reverse --all
git status --short
```
