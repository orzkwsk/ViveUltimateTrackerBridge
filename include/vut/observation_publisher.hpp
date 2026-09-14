#pragma once
#include "monaka/protocol/v1/codec.hpp"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string_view>

namespace vut {
// Single polling-thread owner. Send callbacks must be nonblocking.
class ObservationPublisher {
public:
    using Send = std::function<bool(std::string_view)>;
    using Clock = std::function<std::int64_t()>;
    struct Counters { std::uint64_t sent=0, sendErrors=0, encodeErrors=0, dropped=0; };
    ObservationPublisher(std::string session, Send send, Clock clock, std::size_t capacity=512);
    void enqueue(monaka::protocol::v1::Envelope);
    void flush(std::size_t budget=8);
    void clear();
    std::size_t pending() const { return latest_.size(); }
    const Counters& counters() const { return counters_; }
private:
    using Key = std::pair<std::string, bool>;
    std::string session_;
    Send send_;
    Clock clock_;
    std::size_t capacity_;
    std::map<Key, monaka::protocol::v1::Envelope> latest_;
    std::deque<Key> order_;
    Counters counters_;
};
class LoopbackSender {
public:
    explicit LoopbackSender(std::uint16_t port=29810);
    ~LoopbackSender();
    LoopbackSender(const LoopbackSender&) = delete;
    LoopbackSender& operator=(const LoopbackSender&) = delete;
    bool send(std::string_view) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
