#include "vut/assignment_lock.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace vut::setup::provisional {
struct WriterLock::Impl {
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~Impl() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
    int handle = -1;
    ~Impl() { if (handle >= 0) ::close(handle); }
#endif
};
WriterLock::WriterLock(const std::filesystem::path& sidecar) : impl_(std::make_unique<Impl>()) {
    auto path = sidecar; path += ".lock";
#ifdef _WIN32
    impl_->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (impl_->handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("assignment writer lock unavailable: " + path.string());
#else
    impl_->handle = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (impl_->handle < 0 || ::flock(impl_->handle, LOCK_EX | LOCK_NB) != 0)
        throw std::runtime_error("assignment writer lock unavailable: " + path.string());
#endif
}
WriterLock::~WriterLock() = default;
}
