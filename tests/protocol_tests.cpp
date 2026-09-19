#include "vut/protocol.hpp"

#include <bit>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
void putFloat(std::vector<std::uint8_t>& b, std::size_t off, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    b[off + 0] = static_cast<std::uint8_t>(bits);
    b[off + 1] = static_cast<std::uint8_t>(bits >> 8);
    b[off + 2] = static_cast<std::uint8_t>(bits >> 16);
    b[off + 3] = static_cast<std::uint8_t>(bits >> 24);
}
void putU16(std::vector<std::uint8_t>& b, std::size_t off, std::uint16_t value) {
    b[off] = static_cast<std::uint8_t>(value);
    b[off + 1] = static_cast<std::uint8_t>(value >> 8);
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
}

int main(int argc, char** argv) try {
    if (argc == 2 && std::string(argv[1]) == "--negative-fixture")
        CHECK(vut::protocol::halfToFloat(0x0001) == 0.0f);
    CHECK(vut::protocol::halfToFloat(0x0001) == std::ldexp(1.0f, -24));
    CHECK(vut::protocol::halfToFloat(0x03ff) == std::ldexp(1023.0f, -24));
    CHECK(vut::protocol::halfToFloat(0x0400) == std::ldexp(1.0f, -14));
    CHECK(vut::protocol::halfToFloat(0x0000) == 0.0f);
    CHECK(!std::signbit(vut::protocol::halfToFloat(0x0000)));
    CHECK(std::signbit(vut::protocol::halfToFloat(0x8000)));
    CHECK(std::isinf(vut::protocol::halfToFloat(0x7c00)));
    CHECK(std::isinf(vut::protocol::halfToFloat(0xfc00)));
    CHECK(std::signbit(vut::protocol::halfToFloat(0xfc00)));
    CHECK(std::isnan(vut::protocol::halfToFloat(0x7e00)));

    CHECK(near(vut::protocol::halfToFloat(0x3c00), 1.0f));
    CHECK(near(vut::protocol::halfToFloat(0xc000), -2.0f));

    std::vector<std::uint8_t> report(12 + 0x25, 0);
    report[0] = vut::protocol::kResponseTrackerIncoming;
    putU16(report, 1, 7);
    const std::uint8_t mac[6] = {0x23, 0x32, 0x42, 0xb7, 0x82, 0xd3};
    std::memcpy(report.data() + 3, mac, 6);
    putU16(report, 9, vut::protocol::kIncomingPose);
    report[11] = 0x25;

    report[12] = 4;
    report[13] = 0x01;
    putFloat(report, 14, 1.25f);
    putFloat(report, 18, -0.5f);
    putFloat(report, 22, 2.0f);
    putU16(report, 26, 0x3c00);
    putU16(report, 28, 0x0000);
    putU16(report, 30, 0x0000);
    putU16(report, 32, 0x0000);
    report[48] = 2;

    const auto env = vut::protocol::parseIncomingEnvelope(report);
    CHECK(env);
    CHECK(env->device.slot == 2);
    const auto pose = vut::protocol::parsePose(*env, 1234);
    CHECK(pose);
    CHECK(pose->positionValid);
    CHECK(pose->orientationValid);
    CHECK(near(pose->positionMeters.x, 1.25f));
    CHECK(near(pose->positionMeters.y, -0.5f));
    CHECK(near(pose->positionMeters.z, 2.0f));
    CHECK(near(pose->orientationXyzw.w, 1.0f));
    CHECK(pose->observedMonotonicNs == 1234);

    report.resize(51); report[11]=39;
    auto extended=vut::protocol::parseIncomingEnvelope(report); CHECK(extended);
    CHECK(vut::protocol::parsePose(*extended,1234));
    for (std::size_t len=0; len<51; ++len) {
        std::vector<std::uint8_t> truncated(report.begin(),report.begin()+len);
        CHECK(!vut::protocol::parseIncomingEnvelope(truncated));
    }
    report[11]=38;
    auto badLength=vut::protocol::parseIncomingEnvelope(report); CHECK(badLength);
    CHECK(!vut::protocol::parsePose(*badLength,0));
    std::vector<std::uint8_t> pair(12); pair[0]=0x18; pair[1]=12;
    std::memcpy(pair.data()+6,mac,6);
    auto paired=vut::protocol::parsePairEvent(pair); CHECK(paired && !paired->unpair);
    CHECK(paired->device.toString()=="23:32:42:b7:82:d3");
    pair[5]=1; CHECK(vut::protocol::parsePairEvent(pair)->unpair);
    pair.insert(pair.begin(),0); CHECK(vut::protocol::parsePairEvent(pair));
    pair.resize(10); CHECK(!vut::protocol::parsePairEvent(pair));
    const std::vector<std::uint8_t> payload{1,2,3};
    auto command=vut::protocol::buildFeatureCommand(0x18,payload);
    CHECK(command.size()==65 && command[0]==0 && command[1]==0x18 && command[2]==5 && command[3]==1);
    CHECK(vut::protocol::buildFeatureCommand(0x18,payload,5).empty());
    auto pws0=vut::protocol::buildRfBehaviorPayload(vut::protocol::kRfBehaviorRxPowerSaving,false,0);
    CHECK(pws0 && *pws0==std::array<std::uint8_t,7>{1,0,1,0,0,0,0});
    auto pws2=vut::protocol::buildRfBehaviorPayload(vut::protocol::kRfBehaviorRxPowerSaving,false,2);
    CHECK(pws2 && *pws2==std::array<std::uint8_t,7>{1,0,0,0,1,0,0});
    auto pwsAll=vut::protocol::buildRfBehaviorPayload(vut::protocol::kRfBehaviorRxPowerSaving,false,-1);
    CHECK(pwsAll && *pwsAll==std::array<std::uint8_t,7>{1,0,1,1,1,1,1});
    CHECK(!vut::protocol::buildRfBehaviorPayload(vut::protocol::kRfBehaviorRxPowerSaving,false,5));
    CHECK(!vut::protocol::buildRfBehaviorPayload(vut::protocol::kRfBehaviorRxPowerSaving,false,-2));
    auto ack=vut::protocol::buildAckTx(paired->device,"ARI1");
    CHECK(ack.size()==14 && ack[0]==4 && ack[10]=='A' && ack[13]=='1');
    CHECK(vut::protocol::buildAckTx(paired->device,std::string(45,'x')).empty());
    std::cout << "protocol tests passed\n";
    return 0;
}

catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
