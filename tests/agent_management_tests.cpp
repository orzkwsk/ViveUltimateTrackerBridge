#include "vut/agent_control.hpp"
#include "fake_pairing_transport.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)
using namespace vut;
using Json = nlohmann::json;
struct Fixture {
    pairing_test::Transport* transport = nullptr;
    std::int64_t offset = 0;
    std::uint32_t revision = 0;
    std::uint8_t packet = 0;
    std::unique_ptr<ServiceRuntime> runtime;
    explicit Fixture(bool enabled) {
        auto fake = std::make_unique<pairing_test::Transport>(); transport = fake.get();
        ServiceRuntimeConfig config;
        config.allowExplicitPairing = enabled;
        config.observation.sourceId = "offline-agent";
        config.observation.spaces = {{pairing_test::device().toString(), "offline-map"}};
        config.observation.spaces.emplace("23:32:42:b7:82:d3", "offline-pose-map");
        ServiceRuntimeDependencies deps;
        deps.clock = [&] { return pairing_test::nowNs() + offset; };
        deps.advanceRevision = [&] { return ++revision; };
        deps.makeSessionId = [] { return "00000000-0000-4000-8000-000000000001"; };
        deps.send = [](std::string_view) { return true; };
        runtime = std::make_unique<ServiceRuntime>(Backend(std::move(fake)), config, deps);
        CHECK(runtime->start().ok()); CHECK(transport->writes.empty());
    }
    Json request(const std::string& operation, Json extra = Json::object()) {
        extra["version"] = 1; extra["id"] = "case"; extra["operation"] = operation;
        auto reply = Json::parse(agentControlRequest(*runtime, extra.dump()));
        CHECK(reply["version"] == 1 && reply["id"] == "case"); return reply;
    }
    void pose(std::uint8_t state) {
        std::ifstream file(std::string(FIXTURE_DIR)+"/pose37.bin",std::ios::binary);
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
        CHECK(bytes.size()>48);bytes[12]=++packet;bytes[48]=state;
        transport->reads.push_back(std::move(bytes));CHECK(runtime->pollOnce());
    }
};
int main() try {
    {
        Fixture f(false);
        f.pose(0x12);auto result=f.request("refresh");
        CHECK(result["devices"].size()==1 && result["devices"][0]["modality"]=="Pos+Q");
        CHECK(result["devices"][0]["positionValid"]==true && result["devices"][0]["orientationValid"]==true);
        f.pose(0x14);result=f.request("refresh");
        CHECK(result["devices"][0]["modality"]=="Q-only" && result["devices"][0]["positionValid"]==false && result["devices"][0]["orientationValid"]==true);
        f.pose(0x13);result=f.request("refresh");
        CHECK(result["devices"][0]["modality"]=="NONE" && result["devices"][0]["orientationValid"]==false);
        f.pose(0x22);result=f.request("refresh");
        CHECK(result["devices"][0]["tracking"]=="Unknown" && result["devices"][0]["orientationValid"]==false);
        f.pose(0x12);f.offset+=600000000;result=f.request("refresh");
        CHECK(result["devices"][0]["modality"]=="STALE" && result["devices"][0]["positionValid"].is_null());
        CHECK(f.transport->writes.empty());
    }
    {
        Fixture f(false);
        CHECK(!f.request("begin_pairing", {{"selector",2},{"timeoutSeconds",60}})["ok"].get<bool>());
        CHECK(f.transport->writes.empty());
        CHECK(f.request("identify")["ok"].get<bool>());
        CHECK(f.request("refresh")["identifying"].get<bool>());
        CHECK(f.transport->writes.empty());
        CHECK(!f.request("save_map")["ok"].get<bool>());
        CHECK(!f.request("refresh", {{"unexpected",true}})["ok"].get<bool>());
        CHECK(!Json::parse(agentControlRequest(*f.runtime, "not-json"))["ok"].get<bool>());
    }
    {
        Fixture f(true);
        auto result = f.request("begin_pairing", {{"selector",2},{"timeoutSeconds",60}});
        CHECK(result["ok"].get<bool>() && result["pairingActive"].get<bool>());
        const auto writes = f.transport->writes.size(); CHECK(writes > 0);
        CHECK(!f.request("begin_pairing", {{"selector",2},{"timeoutSeconds",60}})["ok"].get<bool>());
        CHECK(!f.request("identify")["ok"].get<bool>());
        CHECK(f.transport->writes.size() == writes);
        result = f.request("cancel_pairing");
        CHECK(result["ok"].get<bool>() && !result["pairingActive"].get<bool>() && result["cleanupConfirmed"].get<bool>());
        CHECK(f.request("begin_pairing", {{"selector",2},{"timeoutSeconds",1}})["ok"].get<bool>());
        f.offset += 2000000000;
        CHECK(f.runtime->pollOnce());
        result = f.request("refresh");
        CHECK(result["pairing"] == "TimedOut" && result["cleanupConfirmed"].get<bool>());
    }
    {
        Fixture f(true);
        CHECK(f.request("begin_pairing", {{"selector",2},{"timeoutSeconds",60}})["ok"].get<bool>());
        f.transport->reads.push_back(pairing_test::pairReport());
        CHECK(f.runtime->pollOnce());
        auto result = f.request("refresh");
        CHECK(result["pairing"] == "Paired event observed");
        CHECK(result["devices"].size() == 1);
        CHECK(result["devices"][0]["stableId"].is_null());
        CHECK(result["devices"][0]["positionValid"].is_null());
        CHECK(result["devices"][0]["connection"] == "Paired event observed");
        CHECK(f.request("identify")["ok"].get<bool>());
        CHECK(f.request("refresh")["identifyCandidates"].empty());
    }
    {
        Fixture f(true);
        CHECK(f.request("begin_pairing", {{"selector",2},{"timeoutSeconds",60}})["ok"].get<bool>());
        f.transport->uncertainDisable = true;
        auto result = f.request("cancel_pairing");
        CHECK(!result["ok"].get<bool>() && !result["cleanupConfirmed"].get<bool>());
        const auto count = f.transport->writes.size();
        CHECK(!f.request("begin_pairing", {{"selector",2},{"timeoutSeconds",60}})["ok"].get<bool>());
        CHECK(f.transport->writes.size() == count);
        f.runtime->shutdown();
        CHECK(f.runtime->snapshot()->state == ServiceRuntimeState::Faulted);
    }
    std::cout << "PASS default no-send, explicit Pair/duplicate/Cancel/timeout, independent pair evidence, passive Identify, unknown/cleanup refusal\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
