#include "vut/observation_publisher.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#endif
namespace vut {
namespace c1 = monaka::protocol::v2;
ObservationPublisher::ObservationPublisher(std::string session, Send send, Clock clock, std::size_t capacity)
    : session_(std::move(session)), send_(std::move(send)), clock_(std::move(clock)), capacity_(capacity) {
    if (!capacity || capacity > 512) throw std::invalid_argument("queue capacity must be 1..512");
}
void ObservationPublisher::clear() { latest_.clear(); order_.clear(); }
void ObservationPublisher::enqueue(c1::Envelope envelope) {
    const auto* pose = std::get_if<c1::TrackerObservation>(&envelope);
    const auto* state = std::get_if<c1::ObservationDeviceState>(&envelope);
    if ((!pose && !state) || (pose ? pose->session_id : state->session_id) != session_) { ++counters_.dropped; return; }
    const Key key{pose ? pose->device_id : state->device_id, bool(pose)};
    if (state && state->presence == "absent") {
        const Key poseKey{state->device_id, true};
        latest_.erase(poseKey);
        std::erase(order_, poseKey);
    }
    if (!latest_.contains(key)) {
        if (latest_.size() >= capacity_) { ++counters_.dropped; return; }
        order_.push_back(key);
    }
    latest_.insert_or_assign(key, std::move(envelope));
}
void ObservationPublisher::flush(std::size_t budget) {
    while (budget-- && !order_.empty()) {
        const auto key = order_.front(); order_.pop_front();
        auto value = std::move(latest_.at(key)); latest_.erase(key);
        std::string bytes; c1::Error error;
        try {
            const auto now = clock_();
            std::visit([&](auto& v) { v.sent_at_ns = now; }, value);
            if (!c1::EncodeEnvelope(value, bytes, error)) { ++counters_.encodeErrors; continue; }
        } catch (...) { ++counters_.encodeErrors; continue; }
        try {
            if (send_(bytes)) ++counters_.sent;
            else ++counters_.sendErrors;
        } catch (...) { ++counters_.sendErrors; }
        // Never retry old samples on receiver recovery; next admission supplies latest state.
    }
}
struct LoopbackSender::Impl {
#ifdef _WIN32
    SOCKET socket = INVALID_SOCKET;
    bool initialized = false;
#else
    int socket = -1;
#endif
    sockaddr_in target{};
    ~Impl() {
#ifdef _WIN32
        if (socket != INVALID_SOCKET) closesocket(socket);
        if (initialized) WSACleanup();
#else
        if (socket >= 0) close(socket);
#endif
    }
};
LoopbackSender::LoopbackSender(std::uint16_t port) : impl_(std::make_unique<Impl>()) {
    if (!port) throw std::invalid_argument("zero destination port");
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("WSAStartup failed");
    impl_->initialized = true;
#endif
    impl_->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    u_long nonblocking = 1;
    if (impl_->socket == INVALID_SOCKET || ioctlsocket(impl_->socket, FIONBIO, &nonblocking))
        throw std::runtime_error("nonblocking UDP socket failed");
#else
    if (impl_->socket < 0 || fcntl(impl_->socket, F_SETFL, O_NONBLOCK) < 0)
        throw std::runtime_error("nonblocking UDP socket failed");
#endif
    sockaddr_in local{}; local.sin_family = AF_INET; local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(impl_->socket, reinterpret_cast<const sockaddr*>(&local), sizeof(local)))
        throw std::runtime_error("loopback source bind failed");
    impl_->target.sin_family = AF_INET;
    impl_->target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    impl_->target.sin_port = htons(port);
}
LoopbackSender::~LoopbackSender() = default;
bool LoopbackSender::send(std::string_view bytes) noexcept {
    if (bytes.size() > 4096) return false;
    return sendto(impl_->socket, bytes.data(), static_cast<int>(bytes.size()), 0,
        reinterpret_cast<const sockaddr*>(&impl_->target), sizeof(impl_->target)) == static_cast<int>(bytes.size());
}
}
