#include "atlas/core/status/status.h"

#include <utility>

namespace atlas {
namespace core {

Status::Status(ErrorCode code, std::string message)
    : code_(code), message_(std::move(message)) {}

Status Status::Ok() {
  return Status();
}

Status Status::InvalidArgument(std::string message) {
  return Status(ErrorCode::kInvalidArgument, std::move(message));
}

Status Status::NotFound(std::string message) {
  return Status(ErrorCode::kNotFound, std::move(message));
}

Status Status::AlreadyExists(std::string message) {
  return Status(ErrorCode::kAlreadyExists, std::move(message));
}

Status Status::Unavailable(std::string message) {
  return Status(ErrorCode::kUnavailable, std::move(message));
}

Status Status::Unimplemented(std::string message) {
  return Status(ErrorCode::kUnimplemented, std::move(message));
}

Status Status::Internal(std::string message) {
  return Status(ErrorCode::kInternal, std::move(message));
}

}  // namespace core
}  // namespace atlas
