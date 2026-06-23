#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>

#include "runtime/session.h"
#include "runtime/status.h"

namespace uvr {

class SessionPool {
 public:
  SessionPool() = default;
  explicit SessionPool(std::vector<SessionPtr> sessions);

  Status Acquire(SessionPtr& session);
  void Release(SessionPtr session);
  size_t Size() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<SessionPtr> available_;
};

class ScopedSession {
 public:
  ScopedSession(SessionPool& pool, SessionPtr session)
      : pool_(&pool), session_(std::move(session)) {}
  ScopedSession(const ScopedSession&) = delete;
  ScopedSession& operator=(const ScopedSession&) = delete;
  ScopedSession(ScopedSession&& other) noexcept
      : pool_(other.pool_), session_(std::move(other.session_)) {
    other.pool_ = nullptr;
  }
  ScopedSession& operator=(ScopedSession&& other) noexcept;
  ~ScopedSession();

  ISession* operator->() const { return session_.get(); }
  SessionPtr get() const { return session_; }

 private:
  SessionPool* pool_ = nullptr;
  SessionPtr session_;
};

}  // namespace uvr
