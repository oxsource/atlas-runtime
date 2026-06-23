#pragma once

#include <string>

namespace atlas {
namespace core {

enum class ErrorCode {
  kOk = 0,
  kInvalidArgument,
  kNotFound,
  kAlreadyExists,
  kUnavailable,
  kUnimplemented,
  kInternal,
};

class Status {
 public:
  Status() = default;
  Status(ErrorCode code, std::string message);

  static Status Ok();
  static Status InvalidArgument(std::string message);
  static Status NotFound(std::string message);
  static Status AlreadyExists(std::string message);
  static Status Unavailable(std::string message);
  static Status Unimplemented(std::string message);
  static Status Internal(std::string message);

  bool ok() const { return code_ == ErrorCode::kOk; }
  ErrorCode code() const { return code_; }
  const std::string& message() const { return message_; }

 private:
  ErrorCode code_ = ErrorCode::kOk;
  std::string message_;
};

}  // namespace core
}  // namespace atlas
