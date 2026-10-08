#pragma once
#include <filesystem>
#include <memory>

namespace vut::setup::provisional {
// Stable lock-file inode/path; ownership is an OS handle lock, not existence.
// Never unlink it: doing so would allow concurrent owners on different inodes.
class WriterLock {
public:
    explicit WriterLock(const std::filesystem::path& sidecar);
    ~WriterLock();
    WriterLock(const WriterLock&) = delete;
    WriterLock& operator=(const WriterLock&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
