// 连接的优雅关闭队列
//
// 直接 CloseNow() 会丢弃写缓冲里尚未发出的数据：大响应 / 高并发下表现为
// 对端收到截断或空响应。Retire() 的语义是"停止转发 -> 刷完写缓冲 -> 关闭"。
//
// 难点是生命周期：连接关闭后必须有人继续持有 shared_ptr，否则析构函数会
// 立刻 close(socket)，缓冲里的数据照样丢掉。这里由 draining_ 持有强引用，
// 真正的释放推迟到事件循环的下一轮（不能在连接自己的回调栈里析构它）。
#pragma once
#ifndef ORSEND_CONNECTIONREAPER_H
#define ORSEND_CONNECTIONREAPER_H

#include <memory>
#include <vector>

#include "EventLoop.h"
#include "TcpConnection.h"

namespace orsend {

class ConnectionReaper {
public:
    explicit ConnectionReaper(EventLoop& loop);
    ~ConnectionReaper();

    ConnectionReaper(const ConnectionReaper&) = delete;
    ConnectionReaper& operator=(const ConnectionReaper&) = delete;

    // 停止转发并优雅关闭；调用方可以立即丢弃自己的引用
    void Retire(const TcpConnection::Ptr& conn);

    // 进程/组件退出时调用，立即释放所有还在刷数据的连接
    void Clear();

    size_t Draining() const { return draining_.size(); }

private:
    void Sweep();

    EventLoop& loop_;
    std::vector<TcpConnection::Ptr> draining_;
    // 析构后到达的 Post 回调用它判断对象是否还活着
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
};

}  // namespace orsend

#endif  // ORSEND_CONNECTIONREAPER_H
