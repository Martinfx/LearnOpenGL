#pragma once
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {

// Watches the asset files under a folder for changes (a background thread compares
// modification times every `interval` seconds; portable, no OS notification API).
// changes() hands the files written since the last call to the main thread.
class FileWatcher {
public:
    explicit FileWatcher(std::filesystem::path root, double intervalSeconds = 0.5);
    ~FileWatcher();
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    std::vector<std::filesystem::path> changes(); // absolute, each once
    const std::filesystem::path& root() const { return root_; }

private:
    void run();
    std::filesystem::path root_;
    double interval_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stop_ = false;
    std::vector<std::filesystem::path> pending_;
    std::thread thread_;
};

} // namespace engine
