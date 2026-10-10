#include "../tools/research/q1e_mapping_session.hpp"
#include "fixtures/observed_reconnect.hpp"
#include <iostream>
#include <stdexcept>

#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)
using namespace vut::q1e_hil;
class Fake final : public vut::detail::BackendTransport {
public:
    bool open(std::uint16_t,std::uint16_t,bool,std::string&) override { ++opens; return true; }
    void close() override {}
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> value) override {
        writes.emplace_back(value.begin(),value.end()); if (writeThrows) throw std::runtime_error("write exception"); return accepted;
    }
    std::vector<std::uint8_t> getFeature() override { ++gets; if (getThrows) throw std::runtime_error("query exception"); return response; }
    std::vector<std::uint8_t> read(int) override { return {}; }
    bool readFailed() const noexcept override { return false; }
    unsigned opens = 0, gets = 0; bool accepted = true, writeThrows = false, getThrows = false;
    std::vector<std::vector<std::uint8_t>> writes;
    std::vector<std::uint8_t> response{reconnect_fixture::kSequence[4].report.begin(),reconnect_fixture::kSequence[4].report.end()};
};
auto unknownAck() {
    auto p = reconnect_fixture::kSequence[6].report; p[12]=4; std::fill(p.begin()+13,p.end(),0);
    p[13]='T';p[14]='E';p[15]='S';p[16]='T'; return p;
}
auto poseReport() {
    auto p=unknownAck();p[10]=0x10;p[11]=1;p[12]=37;std::fill(p.begin()+13,p.end(),0);p[49]=3;return p;
}
struct Trial {
    Fake hid; std::vector<SendReceipt> receipts; bool auditThrows = false;
    Session session{"synthetic-q1e-run","q1e-token",hid,[&](const SendReceipt& r) {
        receipts.push_back(r); if (auditThrows) throw std::runtime_error("journal exception");
    }};
    Trial() { session.opened(1); session.observe(reconnect_fixture::kSequence[4].report,1000); }
    void baseline() { CHECK(session.requestBOn(10000));CHECK(session.confirmBOn(11000,true)); }
    void proof(std::int64_t now=41000) { session.captureProof("synthetic-q1e-run",1,now,0,now); }
    void arm() { baseline();proof(); }
    Approval approval() { return {"--q1e-once","synthetic-q1e-run","q1e-token","B_ONLY_A_OFF_USB_DISCONNECTED",1}; }
    void freshBurst(std::int64_t now) {
        session.observe(reconnect_fixture::kSequence[5].report,now);
        for(std::size_t i=6;i<15;++i)session.observe(reconnect_fixture::kSequence[i].report,now+i);
        session.observe(poseReport(),now+20);
    }
};
int main(int argc,char**) try {
    CHECK(argc==1);
    {
        Trial t;t.baseline();t.session.tick(90000);
        CHECK(!t.session.stopped() && !t.session.candidate()); // Mapping absence is not a deadline/denial.
        CHECK(t.hid.opens==0 && t.hid.gets==0 && t.hid.writes.empty());
    }
    {
        Trial t; CHECK(!t.session.requestBOn(9999)); CHECK(t.hid.writes.empty());
    }
    {
        Trial t;CHECK(t.session.requestBOn(10000));CHECK(!t.session.confirmBOn(11000,false));CHECK(t.hid.writes.empty());
    }
    for(int fault=0;fault<9;++fault) {
        Trial t;t.arm();auto a=t.approval();std::int64_t now=41000;
        if(fault==0)a.token.clear();if(fault==1)a.token="previous-token";if(fault==2)a.run="previous-run";
        if(fault==3)a.generation=2;if(fault==4)a.flag="--behavior2";if(fault==5)a.physicalConfirmation="A_ON";
        if(fault==6)now=40999;if(fault==7)now=71001;
        if(fault==8)t.session.captureProof("wrong-run",1,41000,0,41000);
        CHECK(!t.session.send(a,now));CHECK(t.hid.writes.empty() && t.hid.gets==0);
    }
    {
        Trial t;t.baseline();CHECK(!t.session.send(t.approval(),41000));CHECK(t.hid.writes.empty());
    }
    {
        Trial t;t.arm();t.session.captureProof("synthetic-q1e-run",1,41000,1,41000);
        CHECK(t.session.stopped() && !t.session.send(t.approval(),41000) && t.hid.writes.empty());
    }
    {
        Trial t;t.arm();CHECK(!t.session.candidate());CHECK(t.session.send(t.approval(),41000));
        CHECK(t.hid.writes.size()==1 && t.hid.gets==1 && t.session.attempts()==1);
        CHECK(t.hid.writes[0]==std::vector<std::uint8_t>(reconnect_fixture::kSequence[3].report.begin(),reconnect_fixture::kSequence[3].report.end()));
        CHECK(t.receipts.size()==2 && !t.receipts[0].attempted && t.receipts[1].attempted && t.receipts[1].accepted);
        CHECK(t.session.responseClass()=="QUALIFIED_MATCH" && t.session.responseRecord());
        CHECK(!t.session.candidate()); // Response record never becomes an address/table/readiness value.
        t.session.sendReturnedAt(45000);t.session.tick(74999);CHECK(!t.session.stopped());
        t.session.tick(75000);CHECK(t.session.reason()=="Q1E_RESPONSE_ONLY");
        CHECK(!t.session.send(t.approval(),80000));CHECK(t.hid.writes.size()==1 && t.hid.gets==1);
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(),41000));CHECK(!t.session.send(t.approval(),41001));
        CHECK(t.session.reason()=="DuplicateQ1ERejected" && t.hid.writes.size()==1 && t.hid.gets==1);
    }
    for(int fault=0;fault<4;++fault) {
        Trial t;t.arm();t.hid.accepted=fault!=0;t.hid.writeThrows=fault==1;t.hid.getThrows=fault==2;t.auditThrows=fault==3;
        CHECK(!t.session.send(t.approval(),41000));CHECK(t.session.attempts()==1);
        CHECK(t.hid.writes.size()==(fault==3?0:1) && t.hid.gets==(fault==2?1:0));
        CHECK(!t.session.send(t.approval(),80000));CHECK(t.hid.writes.size()==(fault==3?0:1));
    }
    {
        Trial t;t.arm();t.hid.response.clear();CHECK(t.session.send(t.approval(),41000));
        CHECK(t.session.responseClass()=="NO_RESPONSE");t.session.sendReturnedAt(42000);
        t.session.tick(71999);CHECK(!t.session.stopped());t.session.tick(72000);CHECK(t.session.reason()=="Q1E_NO_RESPONSE");
        CHECK(t.hid.writes.size()==1 && t.hid.gets==1);
    }
    for(int fault=0;fault<2;++fault) {
        Trial t;t.arm();if(fault==0)t.hid.response[3]^=0x80;
        else {std::fill(t.hid.response.begin(),t.hid.response.end(),0);t.hid.response[1]=0x1e;t.hid.response[2]=2;}
        CHECK(!t.session.send(t.approval(),41000));
        CHECK(t.session.responseClass()==(fault==0?"MALFORMED":"DIFFERENT_VALID_LENGTH"));
        CHECK(t.session.reason()=="UnqualifiedResponseFraming" && t.hid.writes.size()==1 && t.hid.gets==1);
    }
    {
        Trial t;t.arm();t.freshBurst(42000);CHECK(t.session.reason()=="NaturalReconnectObserved");
        CHECK(!t.session.send(t.approval(),43000) && t.hid.writes.empty());
    }
    {
        Trial t;t.arm();t.session.observe(poseReport(),42000);CHECK(t.session.stopped() && t.hid.writes.empty());
    }
    {
        Trial t;t.arm();t.session.observe(reconnect_fixture::kSequence[5].report,42000);
        t.session.observe(reconnect_fixture::kSequence[6].report,42001);t.session.observe(reconnect_fixture::kSequence[7].report,42002);
        CHECK(t.session.freshIdentity() && !t.session.send(t.approval(),43000) && t.hid.writes.empty());
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(),41000));t.session.sendReturnedAt(41010);t.freshBurst(42000);
        CHECK(t.session.recovered() && !t.session.stopped());t.session.tick(71010);
        CHECK(t.session.reason()=="Q1E_ONLY_RECONNECT_CANDIDATE" && t.hid.writes.size()==1 && t.hid.gets==1);
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(),41000));t.session.sendReturnedAt(41010);
        t.session.observe(unknownAck(),42000);CHECK(t.session.candidate());t.session.tick(71010);
        CHECK(t.session.reason()=="Q1E_MAPPING_DISCOVERY_CANDIDATE" && !t.session.pose());
    }
    {
        Trial t;t.arm();t.session.observe(unknownAck(),40500);CHECK(t.session.send(t.approval(),41000));
        CHECK(t.session.candidateBeforeSend());t.session.sendReturnedAt(41010);t.session.tick(71010);
        CHECK(t.session.reason()=="Q1E_EXISTING_CANDIDATE_NO_RECONNECT"); // No newly discovered mapping claim.
    }
    for(int fault=0;fault<4;++fault) {
        Trial t;t.arm();if(fault==0)t.session.observe(reconnect_fixture::kSequence[1].report,42000);
        if(fault==1){t.session.observe(unknownAck(),42000);auto second=unknownAck();second[4]^=1;t.session.observe(second,42001);}
        if(fault==2){t.session.observe(reconnect_fixture::kSequence[5].report,42000);t.session.observe(reconnect_fixture::kSequence[5].report,42001);}
        if(fault==3)t.session.stop("DongleTransportLoss");
        CHECK(t.session.stopped() && !t.session.send(t.approval(),43000) && t.hid.writes.empty());
    }
    {
        Trial t;t.arm();CHECK(t.session.send(t.approval(),41000));t.session.sendReturnedAt(41010);
        auto changed=reconnect_fixture::kSequence[4].report;changed[7]^=1;t.session.observe(changed,42000);t.session.tick(71010);
        CHECK(t.session.reason()=="Q1E_RESPONSE_WITH_OPAQUE_RF_CHANGE" && !t.session.candidate());
    }
    std::cout << "PASS Q1E exact builder, targetless eligibility, approval, one-shot/no-retry, opaque response, baseline/windows, natural reconnect, mutation and isolation\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
