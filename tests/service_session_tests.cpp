#include "vut/service_session.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)

namespace {
struct FakeJournal {
    std::uint32_t current = 0;
    std::vector<std::uint32_t> writes;
    std::uint32_t advance() {
        writes.push_back(++current);
        return current;
    }
};

vut::CoordinateMapIdentity identity() {
    return {"vive-local-1", {{"23:32:03:81:fe:ec", "tracker-0-map"}}};
}
}

int main() try {
    CHECK(vut::parseReconnectRevisionPolicy("strict") == vut::ReconnectRevisionPolicy::Strict);
    CHECK(vut::parseReconnectRevisionPolicy("preserve") ==
          vut::ReconnectRevisionPolicy::PreserveOnTransportReconnect);
    bool invalidPolicy = false;
    try { (void)vut::parseReconnectRevisionPolicy("automatic"); }
    catch (const std::invalid_argument&) { invalidPolicy = true; }
    CHECK(invalidPolicy);

    std::uint64_t sessionCounter = 0;
    std::int64_t epoch = 100;
    const auto session = [&] { return "00000000-0000-0000-0000-" + std::to_string(++sessionCounter); };
    const auto clock = [&] { return ++epoch; };

    // Default construction is strict and preserves the historical behavior.
    FakeJournal strictJournal;
    vut::SessionRevisionCoordinator strict;
    const auto strictA = strict.begin(identity(), vut::SessionStartCause::InitialStart, 1,
        [&] { return strictJournal.advance(); }, session, clock);
    const auto strictB = strict.begin(identity(), vut::SessionStartCause::TransportReconnect, 2,
        [&] { return strictJournal.advance(); }, session, clock);
    CHECK(strictA.revision == 1 && strictA.revisionAdvanced);
    CHECK(strictB.revision == 2 && strictB.revisionAdvanced);
    CHECK(strictA.sessionId != strictB.sessionId && strictA.epochNs != strictB.epochNs);
    CHECK(strictB.transportGeneration == 2);

    FakeJournal journal;
    vut::SessionRevisionCoordinator preserve(vut::ReconnectRevisionPolicy::PreserveOnTransportReconnect);
    const auto begin = [&](const vut::CoordinateMapIdentity& id, vut::SessionStartCause cause,
                           std::uint64_t generation) {
        return preserve.begin(id, cause, generation, [&] { return journal.advance(); }, session, clock);
    };
    const auto first = begin(identity(), vut::SessionStartCause::InitialStart, 10);
    const auto reconnect = begin(identity(), vut::SessionStartCause::TransportReconnect, 11);
    CHECK(first.revision == 1 && first.revisionAdvanced);
    CHECK(reconnect.revision == first.revision && !reconnect.revisionAdvanced);
    CHECK(reconnect.sessionId != first.sessionId);
    CHECK(reconnect.epochNs != first.epochNs);
    CHECK(reconnect.transportGeneration != first.transportGeneration);
    CHECK(journal.writes == std::vector<std::uint32_t>{1});

    // Reset, ambiguous recovery, exhaustion, or any other unproved continuity
    // path is represented by ContinuityUncertain and must reserve a new revision.
    const auto unsafe = begin(identity(), vut::SessionStartCause::ContinuityUncertain, 12);
    CHECK(unsafe.revision == 2 && unsafe.revisionAdvanced);
    const auto safeAgain = begin(identity(), vut::SessionStartCause::TransportReconnect, 13);
    CHECK(safeAgain.revision == 2 && !safeAgain.revisionAdvanced);

    auto sourceChanged = identity(); sourceChanged.sourceId = "vive-local-2";
    CHECK(begin(sourceChanged, vut::SessionStartCause::TransportReconnect, 14).revision == 3);
    auto mapChanged = sourceChanged; mapChanged.spaces.begin()->second = "tracker-1-map";
    CHECK(begin(mapChanged, vut::SessionStartCause::TransportReconnect, 15).revision == 4);
    auto rfChanged = mapChanged;
    const auto map = rfChanged.spaces.begin()->second;
    rfChanged.spaces.clear(); rfChanged.spaces.emplace("24:32:03:81:fe:ec", map);
    CHECK(begin(rfChanged, vut::SessionStartCause::TransportReconnect, 16).revision == 5);
    CHECK(journal.writes == (std::vector<std::uint32_t>{1, 2, 3, 4, 5}));

    // A broken UUID provider fails closed and does not consume a journal value.
    const auto writesBefore = journal.writes;
    bool duplicateSession = false;
    try {
        preserve.begin(rfChanged, vut::SessionStartCause::TransportReconnect, 17,
            [&] { return journal.advance(); }, [&] { return sessionCounter ?
                "00000000-0000-0000-0000-" + std::to_string(sessionCounter) : std::string{}; }, clock);
    } catch (const std::runtime_error&) { duplicateSession = true; }
    CHECK(duplicateSession && journal.writes == writesBefore);

    std::cout << "PASS strict default, preserve reconnect, unsafe/identity advance, monotonic journal, exact RF identity, unique sessions\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
