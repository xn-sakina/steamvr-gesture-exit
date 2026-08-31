#pragma once
#include "app/runtime_interfaces.h"
#include <chrono>
#include <optional>
namespace bailout::platform::windows {
enum class CloseStatus { Idle,GracefulRequested,ExitedGracefully,ForceRequired,ForceSucceeded,Failed,IdentityInvalid };
class ProcessController {
 public:
  static constexpr auto kGracePeriod=std::chrono::seconds(5);
  ~ProcessController(){clear();}
  bool request_graceful_close(const app::RuntimeTarget&,std::chrono::steady_clock::time_point);
  CloseStatus poll(std::chrono::steady_clock::time_point); CloseStatus force_close(); void clear();
  CloseStatus status()const{return status_;}
 private: bool identity_matches()const; bool is_alive()const;
  std::optional<app::RuntimeTarget> target_; void* process_=nullptr; std::chrono::steady_clock::time_point deadline_{}; CloseStatus status_=CloseStatus::Idle;
};
}
