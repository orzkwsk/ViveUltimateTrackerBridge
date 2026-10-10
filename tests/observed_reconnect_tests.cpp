#include "vut/research/observed_reconnect_wire.hpp"
#include "vut/backend.hpp"
#include "vut/backend_transport.hpp"
#include "fixtures/observed_reconnect.hpp"

#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)

namespace {
using reconnect_fixture::Kind;
using reconnect_fixture::kSequence;

class MemoryTransport final : public vut::detail::BackendTransport {
public:
    bool open(std::uint16_t, std::uint16_t, bool, std::string&) override { ++opens; return true; }
    void close() override { ++closes; }
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> packet) override {
        writes.emplace_back(packet.begin(), packet.end()); return true;
    }
    std::vector<std::uint8_t> getFeature() override {
        ++gets;
        std::vector<std::uint8_t> response(65, 0);
        response[1] = writes.back()[1]; response[2] = 4; return response;
    }
    std::vector<std::uint8_t> read(int) override {
        if (reads.empty()) return {};
        auto report = std::move(reads.front()); reads.pop_front(); return report;
    }
    bool readFailed() const noexcept override { return false; }
    int opens = 0, closes = 0, gets = 0;
    std::deque<std::vector<std::uint8_t>> reads;
    std::vector<std::vector<std::uint8_t>> writes;
};

void fakeRuntimeReachability() {
#ifdef _WIN32
    for (const auto policy : {vut::AutomaticVendorActionPolicy::ObserveOnly,
                              vut::AutomaticVendorActionPolicy::Legacy}) {
        auto transport = std::make_unique<MemoryTransport>();
        auto* memory = transport.get();
        // Never feed the fixture's host requests into a sender. Only incoming
        // event/info/response reports enter Backend's existing receive path.
        for (const auto& step : kSequence)
            if (step.kind != Kind::Behavior2 && step.kind != Kind::Query1e)
                memory->reads.emplace_back(step.report.begin(), step.report.end());
        vut::Backend backend(std::move(transport));
        vut::BackendConfig config;
        config.automaticVendorActionPolicy = policy;
        config.configureTrackingOnPair = true; // exercise existing Legacy sends
        CHECK(backend.start(config));
        CHECK(memory->opens == 1 && memory->writes.empty() && memory->gets == 0);
        while (!memory->reads.empty()) CHECK(backend.pollOnce());
        const auto epochs = backend.connectionEpochSnapshot();
        CHECK(epochs.size() == 1 && epochs[0].active && epochs[0].infoComplete);
        CHECK(epochs[0].initializationEligible && epochs[0].reason == vut::ConnectionEpochReason::None);
        backend.stop();
        CHECK(memory->closes >= 1);
        for (const auto& packet : memory->writes) {
            CHECK(packet.size() == 65);
            CHECK(packet[1] != vut::research::kCmdObservedQuery1e);
            CHECK(!(packet[1] == vut::protocol::kCmdRequestRfChangeBehavior &&
                    packet[3] == vut::research::kRfBehaviorObservedCode2));
        }
        if (policy == vut::AutomaticVendorActionPolicy::ObserveOnly)
            CHECK(memory->writes.empty() && memory->gets == 0);
        else CHECK(!memory->writes.empty()); // the audit actually exercised sends
    }
#endif
}
}

int main(int argc, char** argv) try {
    using namespace vut::research;
    CHECK(argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--negative-fixture"));
    auto expectedQuery = kSequence[3].report;
    if (argc == 2) expectedQuery[2] = 2; // deliberate wrong generic-length fixture
    CHECK(buildObservedQuery1eReport() == expectedQuery);
    CHECK(expectedQuery[0] == 0 && expectedQuery[1] == 0x1e && expectedQuery[2] == 1);
    CHECK(std::all_of(expectedQuery.begin()+3, expectedQuery.end(), [](auto v) {return v==0;}));
    // The existing generic builder remains unchanged; it is not this special form.
    CHECK(vut::protocol::buildFeatureCommand(0x1e, {})[2] == 2);
    for (int slot=0; slot<5; ++slot) {
        const auto request = buildObservedBehavior2Report(slot); CHECK(request);
        CHECK((*request)[0] == 0 && (*request)[1] == 0x1d && (*request)[2] == 9);
        CHECK((*request)[3] == 2 && (*request)[4] == 1);
        for (int i=0; i<5; ++i) CHECK((*request)[5+i] == (i==slot ? 1 : 0));
        CHECK(std::all_of(request->begin()+10, request->end(), [](auto v){return v==0;}));
    }
    for (int slot : {-1000,-2,-1,5,1000}) CHECK(!buildObservedBehavior2Report(slot));
    const auto behavior = buildObservedBehavior2Report(reconnect_fixture::kSlot); CHECK(behavior);
    CHECK(*behavior == kSequence[0].report && *behavior == kSequence[2].report);

    const std::array<Kind,15> order{Kind::Behavior2,Kind::Unpaired,Kind::Behavior2,Kind::Query1e,
        Kind::QueryResponse,Kind::Paired,Kind::Info,Kind::Info,Kind::Info,Kind::Info,Kind::Info,
        Kind::Info,Kind::Info,Kind::Info,Kind::Info};
    unsigned behaviors=0, queries=0;
    constexpr std::array<std::string_view,9> tokens{"NANA","NADS","NASS","NASI","NAPI","NAV1","NARI","NAGN","NAZZ"};
    for (std::size_t i=0; i<kSequence.size(); ++i) {
        const auto& step=kSequence[i]; CHECK(step.kind==order[i]);
        behaviors += step.kind==Kind::Behavior2; queries += step.kind==Kind::Query1e;
        if (step.kind==Kind::Unpaired || step.kind==Kind::Paired) {
            const auto event=vut::protocol::parsePairEvent(step.report); CHECK(event);
            CHECK(event->unpair == (step.kind==Kind::Unpaired));
            CHECK(event->device.mac==reconnect_fixture::kAddress && event->device.slot==0);
        } else if (step.kind==Kind::Info) {
            CHECK(step.label==tokens[i-6]);
            const auto envelope=vut::protocol::parseIncomingEnvelope(step.report); CHECK(envelope);
            CHECK(envelope->device.mac==reconnect_fixture::kAddress && envelope->type==vut::protocol::kIncomingAck);
            const std::string_view text(reinterpret_cast<const char*>(envelope->payload.data()), envelope->payload.size());
            CHECK(text.starts_with(step.label));
        }
    }
    CHECK(behaviors==2 && queries==1);
    const auto& response=kSequence[4].report;
    const auto match=matchObserved1eResponseFrame(response); CHECK(match);
    CHECK(matchObserved1eResponseFrame(std::span(response).subspan(1)));
    CHECK(std::equal(match->opaqueRecord.begin(),match->opaqueRecord.end(),response.begin()+7));
    for (std::size_t size=0; size<65; ++size) CHECK(!matchObserved1eResponseFrame(std::span(response).first(size)));
    for (std::size_t offset=0; offset<7; ++offset) {
        auto bad=response; bad[offset]^=0x80; CHECK(!matchObserved1eResponseFrame(bad));
    }
    for (std::size_t offset=30; offset<65; ++offset) {
        auto bad=response; bad[offset]=1; CHECK(!matchObserved1eResponseFrame(bad));
    }
    CHECK(!vut::protocol::parsePairEvent(response) && !vut::protocol::parseIncomingEnvelope(response));

    // Existing PairDevice/PWS and kind-04 ACK representation stays byte-exact.
    const std::array<std::uint8_t,7> pair{0,1,1,0,0,0,0}, pws{1,0,1,0,0,0,0};
    CHECK(*vut::protocol::buildRfBehaviorPayload(0,true,0)==pair);
    CHECK(*vut::protocol::buildRfBehaviorPayload(1,false,0)==pws);
    const vut::DeviceId device{reconnect_fixture::kAddress,0};
    const std::vector<std::uint8_t> ack{4,0xaa,0xb0,0xcc,0xdd,0xee,0xff,0,1,4,'A','R','I','1'};
    CHECK(vut::protocol::buildAckTx(device,"ARI1")==ack);
    const auto report=vut::protocol::buildFeatureCommand(0x18,ack);
    CHECK(report.size()==65 && report[0]==0 && report[1]==0x18 && report[2]==16);
    CHECK(std::equal(ack.begin(),ack.end(),report.begin()+3));
    CHECK(std::all_of(report.begin()+17,report.end(),[](auto v){return v==0;}));
    fakeRuntimeReachability();
    std::cout << "PASS inert observed reconnect bytes, response framing, ordering and fake runtime isolation\n";
    return 0;
} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
