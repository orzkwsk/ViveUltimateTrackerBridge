#include "../tools/research/active_reconnect_session.hpp"
#include "fixtures/observed_reconnect.hpp"
#include <iostream>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)
using namespace vut::active_hil;
class Fake final : public vut::detail::BackendTransport {
public:
    bool open(std::uint16_t,std::uint16_t,bool,std::string&) override { ++opens; return true; }
    void close() override {}
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> value) override {
        writes.emplace_back(value.begin(),value.end()); if (throws) throw std::runtime_error("simulated after-write exception"); return accepted;
    }
    std::vector<std::uint8_t> getFeature() override { ++gets; return response; }
    std::vector<std::uint8_t> read(int) override { return {}; }
    bool readFailed() const noexcept override { return false; }
    bool accepted = true, throws = false;
    unsigned opens = 0, gets = 0;
    std::vector<std::vector<std::uint8_t>> writes;
    std::vector<std::uint8_t> response{reconnect_fixture::kSequence[4].report.begin(), reconnect_fixture::kSequence[4].report.end()};
};
auto candidateReport() {
    auto report = reconnect_fixture::kSequence[6].report;
    report[12] = 4; std::fill(report.begin()+13,report.end(),0);
    report[13]='T';report[14]='E';report[15]='S';report[16]='T'; return report;
}
struct Trial {
    Fake hid;
    std::vector<SendReceipt> receipts;
    bool auditThrows = false;
    Session session{"synthetic-run",{"token-1","token-2","token-3"},hid,[&](const SendReceipt& r) {
        receipts.push_back(r); if (auditThrows) throw std::runtime_error("simulated disk error");
    }};
    Trial() { session.opened(1); }
    void observeCandidate(std::int64_t now) { session.observe(candidateReport(),now); }
    void arm() {
        CHECK(session.requestBOn(5000)); CHECK(session.confirmBOn(6000));
        observeCandidate(16000); CHECK(session.bindB(session.currentBinding(),true,16000));
        session.captureProof("synthetic-run",16000,16000);
    }
    Approval approval(Gate g) {
        return {g,flagName(g),"synthetic-run",g==Gate::B21?"token-1":g==Gate::B22?"token-2":"token-3","B",session.currentBinding()};
    }
};
int main(int argc,char**) try {
    CHECK(argc==1); // Release checks are explicit, never NDEBUG assert.
    CHECK(qualifiesDongleInterface(L"synthetic:VID_0BB4&PID_0350:single-interface"));
    CHECK(qualifiesDongleInterface(L"synthetic:vid_0bb4&pid_0350&mi_00:composite"));
    CHECK(!qualifiesDongleInterface(L"synthetic:vid_0bb4&pid_0350&mi_01:other-interface"));
    CHECK(!qualifiesDongleInterface(L"synthetic:vid_0bb4&pid_0351:other-product"));
    {
        Trial t; t.session.observe(reconnect_fixture::kSequence[4].report,1000);
        CHECK(t.session.rfReports()==1 && !t.session.candidate());
        CHECK(t.hid.writes.empty() && t.hid.gets==0 && t.hid.opens==0); // observe/default has no hardware call
        CHECK(t.session.requestBOn(5000)); CHECK(t.session.confirmBOn(6000));
        CHECK(!t.session.bindB(t.session.currentBinding(),true,16000)); CHECK(t.hid.writes.empty());
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(Gate::B21),17000));
        CHECK(t.hid.writes.size()==1 && t.hid.gets==0 && t.receipts.size()==2);
        CHECK(!t.receipts[0].attempted && t.receipts[1].attempted && t.receipts[1].returned);
        CHECK(t.hid.writes[0]==std::vector<std::uint8_t>(reconnect_fixture::kSequence[0].report.begin(),reconnect_fixture::kSequence[0].report.end()));
        CHECK(!t.session.send(t.approval(Gate::B21),17001)); CHECK(t.session.reason()=="DuplicateSendRejected");
        CHECK(t.hid.writes.size()==1);
    }
    for (int fault=0;fault<10;++fault) {
        Trial t;t.arm();auto approval=t.approval(Gate::B21);
        if(fault==0)approval.token.clear();if(fault==1)approval.token="token-2";
        if(fault==2)approval.binding.device.slot=4;if(fault==3)approval.binding.device.mac[0]^=1;
        if(fault==4)approval.binding.generation=2;if(fault==5)approval.binding.observationEpoch=2;
        if(fault==6)approval.run="previous-run";if(fault==7)approval.flag="--observe";
        if(fault==8)approval.binding.connectionEpoch=1;if(fault==9)approval.alias="A";
        CHECK(!t.session.send(approval,17000));CHECK(t.hid.writes.empty() && t.hid.gets==0);
    }
    for (int fault=0;fault<3;++fault) {
        Trial t;t.arm();t.hid.accepted=fault!=0;t.hid.throws=fault==1;t.auditThrows=fault==2;
        CHECK(!t.session.send(t.approval(Gate::B21),17000));
        CHECK(t.hid.writes.size()==(fault==2?0:1));CHECK(t.session.attempts()[0]==1);
        CHECK(!t.session.send(t.approval(Gate::B21),27000));CHECK(!t.session.send(t.approval(Gate::B22),27000));
        CHECK(t.hid.writes.size()==(fault==2?0:1));CHECK(t.hid.gets==0);
    }
    {
        Trial t;t.arm();CHECK(!t.session.send(t.approval(Gate::B22),17000));CHECK(t.hid.writes.empty());
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(Gate::B21),17000));
        t.session.sendReturnedAt(23000); CHECK(t.session.nextGateAt()==33000);
        CHECK(!t.session.send(t.approval(Gate::B22),17001));CHECK(t.hid.writes.size()==1);
    }
    {
        Trial t;t.arm();CHECK(!t.session.send(t.approval(Gate::B21),20000));CHECK(t.hid.writes.empty()); // stale RF binding
    }
    {
        Trial t;t.arm();t.observeCandidate(47000);CHECK(!t.session.send(t.approval(Gate::B21),47000));CHECK(t.hid.writes.empty());
    }
    {
        Trial t;t.arm();auto mutated=candidateReport();mutated[4]^=1;t.session.observe(mutated,17000);
        CHECK(t.session.reason()=="SlotRfMutationOrAmbiguousTarget");CHECK(!t.session.send(t.approval(Gate::B21),17001));CHECK(t.hid.writes.empty());
    }
    {
        Trial t;t.arm();auto mutated=candidateReport();mutated[5]=0xb4;t.session.observe(mutated,17000);
        CHECK(t.session.reason()=="SlotRfMutationOrAmbiguousTarget" && t.hid.writes.empty());
    }
    {
        Trial t; CHECK(t.session.requestBOn(5000)); t.observeCandidate(5500);
        CHECK(!t.session.stopped() && t.session.candidate()); CHECK(t.session.confirmBOn(6000));
        t.observeCandidate(16000); CHECK(!t.session.bindB(t.session.currentBinding(),false,16000)); CHECK(t.hid.writes.empty());
    }
    {
        Trial t;t.arm();auto pose=candidateReport();pose[10]=0x10;pose[11]=1;pose[12]=37;
        std::fill(pose.begin()+13,pose.end(),0);pose[49]=3;t.session.observe(pose,17000);
        CHECK(t.session.reason()=="NaturalPoseAlreadyActive" && t.session.pose() && t.hid.writes.empty());
    }
    {
        Trial t;t.arm();t.session.transportLost();CHECK(!t.session.send(t.approval(Gate::B21),17000));CHECK(t.hid.writes.empty());
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(Gate::B21),17000));
        t.session.observe(reconnect_fixture::kSequence[1].report,17001);
        t.session.observe(reconnect_fixture::kSequence[1].report,17002);
        CHECK(t.session.reason()=="UnexpectedUnpairOrRegistrationLoss");CHECK(t.hid.writes.size()==1);
    }
    {
        Trial t;t.arm();auto malformed=candidateReport();malformed[12]=255;t.session.observe(malformed,17000);
        CHECK(t.session.stopped() && t.hid.writes.empty());
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(Gate::B21),17000));
        t.session.observe(reconnect_fixture::kSequence[1].report,17001);
        t.observeCandidate(27000);CHECK(t.session.send(t.approval(Gate::B22),27000));
        t.observeCandidate(37000);CHECK(t.session.send(t.approval(Gate::Q1E),37000));
        CHECK(t.hid.writes.size()==3 && t.hid.gets==1 && t.session.attempts()==(std::array<unsigned,3>{1,1,1}));
        CHECK(!t.session.send(t.approval(Gate::Q1E),37001));CHECK(t.hid.writes.size()==3 && t.hid.gets==1);
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(Gate::B21),17000));
        t.observeCandidate(27000);t.hid.response[2]^=1;CHECK(!t.session.send(t.approval(Gate::Q1E),27000));
        CHECK(t.session.reason()=="MalformedQueryResponse" && t.hid.writes.size()==2 && t.hid.gets==1);
        CHECK(!t.session.send(t.approval(Gate::Q1E),37000));CHECK(t.hid.writes.size()==2 && t.hid.gets==1);
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(Gate::B21),17000));
        t.session.observe(reconnect_fixture::kSequence[5].report,17010);
        for(std::size_t i=6;i<15;++i)t.session.observe(reconnect_fixture::kSequence[i].report,17020+i);
        auto pose=candidateReport();pose[10]=0x10;pose[11]=1;pose[12]=37;std::fill(pose.begin()+13,pose.end(),0);pose[49]=3;
        t.session.observe(pose,17100);CHECK(t.session.reason()=="HIL-A" && t.session.recovered());
        CHECK(!t.session.send(t.approval(Gate::B22),27000));CHECK(!t.session.send(t.approval(Gate::Q1E),27000));
        CHECK(t.hid.writes.size()==1 && t.hid.gets==0); // Lost pose arrival is not Ready; no extra sends
    }
    std::cout<<"PASS observe-only, one trial/gate, no retry, duplicate, wrong slot, approval, mutation, transport, framing, stop-after-reconnect\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
