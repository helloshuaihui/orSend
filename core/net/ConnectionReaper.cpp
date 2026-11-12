#include "ConnectionReaper.h"

#include <algorithm>

#include "Logger.h"

namespace orsend {

ConnectionReaper::ConnectionReaper(EventLoop& loop) : loop_(loop) {}

ConnectionReaper::~ConnectionReaper() { Clear(); }

void ConnectionReaper::Retire(const TcpConnection::Ptr& conn) {
    if (!conn) return;

    // 先摘掉回调，避免关闭过程中又触发一次 CloseStream 造成递归
    conn->SetOnData(nullptr);
    conn->SetOnClose(nullptr);

    if (conn->Closed()) return;

    draining_.push_back(conn);
    conn->Close();

    // 延迟释放：此刻可能正处在 conn 自己的回调栈里，不能就地析构
    std::weak_ptr<int> alive = alive_;
    loop_.Post([this, alive]() {
        if (alive.expired()) return;
        Sweep();
    });
}

void ConnectionReaper::Clear() { draining_.clear(); }

void ConnectionReaper::Sweep() {
    draining_.erase(std::remove_if(draining_.begin(), draining_.end(),
                                   [](const TcpConnection::Ptr& c) { return !c || c->Closed(); }),
                    draining_.end());
    if (!draining_.empty()) {
        LOG_DEBUG("net", "仍有 " << draining_.size() << " 个连接在刷写缓冲");
    }
}

}  // namespace orsend
