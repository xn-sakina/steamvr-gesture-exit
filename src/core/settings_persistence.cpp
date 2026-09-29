#include "core/settings_persistence.hpp"

#include <utility>

namespace steamvr_gesture_exit::core {

SettingsPersistenceWorker::SettingsPersistenceWorker(std::filesystem::path path)
    : store_(std::move(path)), thread_([this] { run(); }) {}

SettingsPersistenceWorker::~SettingsPersistenceWorker() {
  stop();
}

bool SettingsPersistenceWorker::enqueue(SettingsChange change) {
  {
    std::scoped_lock lock(mutex_);
    if (stop_ || outstanding_ == kMaxOutstanding)
      return false;
    pending_.push_back(std::move(change));
    ++outstanding_;
  }
  wake_.notify_one();
  return true;
}

std::vector<SettingsSaveResult> SettingsPersistenceWorker::take_results() {
  std::vector<SettingsSaveResult> results;
  std::scoped_lock lock(mutex_);
  results.reserve(completed_.size());
  while (!completed_.empty()) {
    results.push_back(std::move(completed_.front()));
    completed_.pop_front();
  }
  outstanding_ -= results.size();
  return results;
}

void SettingsPersistenceWorker::stop() {
  // join() is not safe when two shutdown paths call stop concurrently.
  std::scoped_lock stop_lock(stop_mutex_);
  {
    std::scoped_lock lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable())
    thread_.join();
}

void SettingsPersistenceWorker::run() {
  while (true) {
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this] { return stop_ || !pending_.empty(); });
    if (pending_.empty())
      return;
    auto change = std::move(pending_.front());
    pending_.pop_front();
    lock.unlock();

    const auto started_at = std::chrono::steady_clock::now();
    std::string error;
    bool saved = false;
    try {
      saved = store_.save(change.settings, &error);
    } catch (...) {
      error = "SETTINGS_WRITE_FAILED";
    }
    const auto completed_at = std::chrono::steady_clock::now();
    lock.lock();
    completed_.push_back({std::move(change), saved, std::move(error), started_at, completed_at});
  }
}

}  // namespace steamvr_gesture_exit::core
