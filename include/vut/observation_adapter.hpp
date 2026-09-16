#pragma once
#include "vut/types.hpp"
#include "monaka/protocol/v2/codec.hpp"
#include <map>

namespace vut {
struct ObservationContext {
    std::string sourceId, sessionId;
    std::uint64_t generation = 0;
    std::uint32_t revision = 0;
    std::int64_t epochNs = 0;
    // Explicit per-MAC map identities; no axis-name or slot based fallback.
    std::map<std::string, std::string> spaces;
};
class ViveObservationAdapter {
public:
    explicit ViveObservationAdapter(ObservationContext context);
    static monaka::protocol::v2::TrackerObservation mapPose(
        const TrackerState&, const ObservationContext&, std::int64_t sequence);
    std::optional<monaka::protocol::v2::Envelope> admit(const BackendEvent&);
    bool restartRequired() const { return restartRequired_; }
    std::uint64_t rejected() const { return rejected_; }
private:
    struct Stream {
        std::int64_t poseSequence = 0, stateSequence = 0, lastPoseNs = -1, lastStateNs = -1;
        std::uint8_t rf = 0;
        bool absent = false;
    };
    ObservationContext context_;
    std::map<std::string, Stream> streams_;
    std::uint64_t rejected_ = 0;
    bool restartRequired_ = false;
};
}
