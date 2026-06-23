#include "runtime/session_pool.h"

namespace uvr {

SessionPool::SessionPool(std::vector<SessionPtr> sessions) {
  for (auto& session : sessions) {
    if (session) {
      available_.push(std::move(session));
    }
  }
}

Status SessionPool::Acquire(SessionPtr& session) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (available_.empty()) {
    return Status::Unavailable("no session is available");
  }
  session = available_.front();
  available_.pop();
  return Status::Ok();
}

void SessionPool::Release(SessionPtr session) {
  if (!session) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    available_.push(std::move(session));
  }
  cv_.notify_one();
}

size_t SessionPool::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return available_.size();
}

ScopedSession& ScopedSession::operator=(ScopedSession&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  if (pool_ && session_) {
    pool_->Release(std::move(session_));
  }
  pool_ = other.pool_;
  session_ = std::move(other.session_);
  other.pool_ = nullptr;
  return *this;
}

ScopedSession::~ScopedSession() {
  if (pool_ && session_) {
    pool_->Release(std::move(session_));
  }
}

}  // namespace uvr
