### Titan 网络层架构设计总结（实现指南）

#### 1. 核心设计哲学
*   **业务驱动 IO**：逻辑线程（Player）只负责产生数据，不关心网络细节。
*   **增量唤醒（Incremental Wakeup）**：只有往 Session 塞了数据的 Player，才会触发网络发送，杜绝无效轮询。
*   **生命周期与调度分离**：使用双链表解耦，Shared List 管生死，Weak List 管调度。
*   **线程封闭（Thread Confinement）**：所有容器（List/Map）的增删改查仅发生在 NetActor 线程，消除锁竞争。

---

#### 2. 线程模型与职责划分

| 线程类型 | 核心对象 | 职责 |
| :--- | :--- | :--- |
| **Logic Thread(s)** | `PlayerActor`, `Channel` | 1. 业务逻辑 Tick。<br>2. 通过 Channel 序列化数据。<br>3. **向 NetActor 发送 `FlushNotifyMsg` 或 `DestroySessionMsg`（仅发消息，不碰容器）。** |
| **NetActor Thread** | `Server`, `Session`, `Mailbox` | 1. **唯一有权操作容器的线程**。<br>2. 维护双链表和哈希表。<br>3. 调度 `Session::Flush()`。<br>4. 处理 Session 的销毁请求。 |
| **IO Thread Pool** | `Asio::io_context`, `Connection` | 1. 绑定 Socket。<br>2. 执行 `async_write`/`async_send_to` 系统调用。<br>3. 执行回调（回调通过 Strand 保证线程内顺序）。 |

---

#### 3. 数据结构定义

##### 3.1 Session 内部结构
```cpp
struct Session : public boost::intrusive::list_base_hook<> {
    uint64_t uid_;
    std::atomic<uint64_t> conn_gen_{0}; // 连接版本号，防旧回调

    // 发送缓冲区（需互斥锁保护）
    std::mutex reliable_mtx_;
    std::vector<char> reliable_buf_; // TCP/KCP
    std::mutex unreliable_mtx_;
    std::vector<char> unreliable_buf_; // UDP

    // 弱引用节点（用于挂载到 Dispatch List）
    struct DispatchNode : public boost::intrusive::list_base_hook<> {
        std::weak_ptr<Session> wp;
    } dispatch_node_;

    // 连接对象（TCP/UDP）
    std::unique_ptr<TcpConnection> tcp_conn_;
    std::unique_ptr<UdpConnection> udp_conn_;

    // Channel 持有 weak_ptr<Session>，用于往上述缓冲区塞数据
};
```

##### 3.2 Server 全局管理容器
```cpp
class Server {
private:
    // 1. 生命周期链表（Logic/NetActor 线程通过引用计数间接控制）
    //    - 插入：Player 创建成功时，NetActor 线程插入。
    //    - 删除：NetActor 线程执行 DestroySessionImpl 时删除。
    boost::intrusive::list<Session> lifecycle_list_;

    // 2. 调度链表（仅 NetActor 线程操作）
    //    - 存储弱引用节点，不参与生命周期管理。
    //    - 仅用于遍历需要 Flush 的 Session。
    boost::intrusive::list<Session::DispatchNode> dispatch_list_;

    // 3. 索引哈希表（仅 NetActor 线程操作）
    //    - Key: UID
    //    - Value: dispatch_list_ 的迭代器（O(1) 查找）
    std::unordered_map<uint64_t, decltype(dispatch_list_)::iterator> uid_map_;

    // NetActor 的邮箱和 Strand（保证单线程执行）
    boost::asio::io_context::strand net_strand_; 
};
```

---

#### 4. 关键数据流与控制流

##### 4.1 发送数据流（上行/下行通用）
1.  **Logic Thread**：`Player::Tick()` 调用 `Channel::Send(data)`。
2.  **Channel**：序列化数据，调用 `Session::AppendReliable()` 或 `AppendUnreliable()`。
3.  **Session**：将数据拷贝到对应的 `reliable_buf_` 或 `unreliable_buf_`（加锁），并设置脏标志。
4.  **Channel**：向 NetActor 线程 **Post** 一个 `FlushNotifyMsg(uid)`（消息体不含数据，仅含 UID）。
5.  **NetActor Thread**：处理消息，通过 `uid_map_` 找到 `DispatchNode`，进而 `lock()` 获取 `shared_ptr<Session>`。若成功，调用 `Session::Flush()`。
6.  **Session::Flush()**：将缓冲区数据移交（Move）给 `Connection` 对象，调用 `connection->AsyncWrite()`。
7.  **IO Thread**：执行底层 Socket 发送。

##### 4.2 连接更换与回调安全（TCP）
1.  Session 断线重连，创建新的 `TcpConnection`，`conn_gen_++`。
2.  `Connection` 构造时拷贝当前的 `conn_gen_`。
3.  **AsyncWrite 回调触发时**：
    ```cpp
    void OnWriteComplete(error_code ec) {
        auto session = session_wp_.lock();
        if (!session || gen_ != session->conn_gen_.load(acq)) 
            return; // 版本号不匹配，丢弃回调
        if (!ec) session->OnSendSuccess();
        else session->OnError();
    }
    ```

##### 4.3 销毁控制流（重点！）
1.  **Logic Thread**：`Player` 析构，**向 NetActor Post 一个 `DestroySessionMsg(uid)`**。
2.  **NetActor Thread**：执行 `DestroySessionImpl(uid)`：
    *   从 `uid_map_` 找到 `dispatch_list_` 迭代器。
    *   从 `dispatch_list_` 移除 `DispatchNode`。
    *   从 `uid_map_` 删除键值对。
    *   **关键**：从 `lifecycle_list_` 移除 `Session` 节点（`erase(iterator_to)`），此时 `shared_ptr` 引用计数归零，`Session` 对象析构。
    *   *注：如果 Session 还有未发送完的可靠数据，可延迟此步骤，直到发送完成或超时。*

---

#### 5. 业务语义与 QoS 策略
*   **Channel 划分**：
    *   `Channel_Reliable` (RPC/交易/登录)：绑定 TCP/KCP，需 ACK/重传。
    *   `Channel_Unreliable_State` (位置同步)：绑定 UDP，只保留最新帧。
    *   `Channel_Unreliable_Voice` (语音)：绑定 UDP，Jitter Buffer，平滑发送。
*   **UDP 发送**：`sendto` 成功后即返回，无应用层回调（除非做可靠 UDP）。
*   **重要业务（如购买）**：必须使用 Reliable Channel，服务端需等待客户端 ACK 确认，不能仅依赖数据库落盘。

---

#### 6. 待实现清单（TODO）
1.  **Intrusive List 封装**：确保 `erase(iterator)` 不会导致迭代器失效。
2.  **Strand 封装**：确保 NetActor 的所有操作都在同一个 Strand 中执行。
3.  **水位控制**：Session 缓冲区满时，丢弃旧 UDP 包或触发 TCP 背压。
4.  **心跳机制**：NetActor 定时扫描 Session 的最后活跃时间，超时触发 `DestroySessionMsg`。

---
**一句话总结实现顺序**：先搭 `Server` 的双链表+哈希表骨架，再实现 `Session` 的缓冲区，接着打通 `Channel -> Session -> NetActor` 的消息通知链路，最后填 `Connection` 的 Asio 回调。

这份总结可以直接作为你 Titan 项目的 `ARCHITECTURE.md` 或者实现 Checklist。如果在写 Intrusive List 或者 Strand 封装时卡住了，随时把代码片段贴出来，我帮你 Review 线程安全和生命周期的细节。祝你 Coding 顺利！🚀