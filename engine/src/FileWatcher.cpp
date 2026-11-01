#include "engine/FileWatcher.h"

#include "engine/AssetRegistry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>

namespace engine {

namespace fs = std::filesystem;

FileWatcher::FileWatcher(fs::path root, double intervalSeconds)
    : root_(fs::absolute(root).lexically_normal()), interval_(std::max(0.05, intervalSeconds))
{
    thread_ = std::thread([this] { run(); });
}

FileWatcher::~FileWatcher()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
        thread_.join();
}

std::vector<fs::path> FileWatcher::changes()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<fs::path> out;
    out.swap(pending_);
    return out;
}

void FileWatcher::run()
{
    std::map<fs::path, fs::file_time_type> seen;
    bool first = true;
    for (;;) {
        std::map<fs::path, fs::file_time_type> now;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root_, fs::directory_options::skip_permission_denied, ec), end; it != end;
             it.increment(ec)) {
            if (ec)
                break;
            if (it->is_directory(ec)) {
                const std::string name = it->path().filename().string();
                if (!name.empty() && name[0] == '.')
                    it.disable_recursion_pending();
                continue;
            }
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (!isAssetExtension(ext) && !canReference(it->path()))
                continue;
            const auto time = it->last_write_time(ec);
            if (!ec)
                now.emplace(it->path().lexically_normal(), time);
        }
        if (!first) {
            std::vector<fs::path> changed;
            for (const auto& [path, time] : now) {
                const auto old = seen.find(path);
                if (old == seen.end() || old->second != time)
                    changed.push_back(path);
            }
            if (!changed.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                for (fs::path& p : changed)
                    if (std::find(pending_.begin(), pending_.end(), p) == pending_.end())
                        pending_.push_back(std::move(p));
            }
        }
        seen = std::move(now);
        first = false;
        std::unique_lock<std::mutex> lock(mutex_);
        if (wake_.wait_for(lock, std::chrono::duration<double>(interval_), [this] { return stop_; }))
            return;
    }
}

} // namespace engine
