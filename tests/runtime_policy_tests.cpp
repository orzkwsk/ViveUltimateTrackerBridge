#include "vut/backend.hpp"
#include "vut/loss_diagnostics.hpp"
#include "vut/service_options.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)

int main() try {
    const vut::BackendConfig production;
    CHECK(!production.diagnosticSlamQueries);
    vut::detail::LossDiagnostics queries;
    vut::TrackerState lost;
    lost.device.slot = 0; lost.trackingStatus = vut::TrackingStatus::Lost;
    for (const auto time : {0LL, 5'000'000'000LL, 60'000'000'000LL}) {
        lost.observedMonotonicNs = time;
        CHECK(!queries.queryDue(lost, production.diagnosticSlamQueries));
    }
    // Enabling diagnostics starts a new interval, never reuses production history.
    CHECK(!queries.queryDue(lost, true));
    lost.observedMonotonicNs += 4'999'999'999LL; CHECK(!queries.queryDue(lost, true));
    ++lost.observedMonotonicNs; CHECK(queries.queryDue(lost, true));
    lost.observedMonotonicNs += 10'000'000'000LL; CHECK(!queries.queryDue(lost, true));
    auto sibling = lost; sibling.device.slot = 2;
    CHECK(!queries.queryDue(sibling, true));
    sibling.observedMonotonicNs += 5'000'000'000LL; CHECK(queries.queryDue(sibling, true));
    CHECK(!queries.queryDue(lost, true)); // Sibling does not reset the first slot.
    queries.forget(0); CHECK(!queries.queryDue(lost, true));
    lost.observedMonotonicNs += 5'000'000'000LL; CHECK(queries.queryDue(lost, true));
    lost.trackingStatus = vut::TrackingStatus::Unknown; CHECK(!queries.queryDue(lost, true));
    lost.trackingStatus = vut::TrackingStatus::RecentlyLost; CHECK(!queries.queryDue(lost, true));
    lost.observedMonotonicNs += 5'000'000'000LL; CHECK(queries.queryDue(lost, true));
    CHECK(!queries.queryDue(lost, false)); CHECK(!queries.queryDue(lost, true));
    queries.clear(); CHECK(!queries.queryDue(lost, true));
    lost.observedMonotonicNs = 1; CHECK(!queries.queryDue(lost, true)); // clock discontinuity
    lost.observedMonotonicNs += 5'000'000'000LL; CHECK(queries.queryDue(lost, true));
    for (int slot : {-1, 5, 1000}) { lost.device.slot = slot; CHECK(!queries.queryDue(lost, true)); }

    auto options = [](std::initializer_list<std::string_view> args) {
        return vut::parseServiceOptions(std::span<const std::string_view>(args.begin(), args.size()));
    };
    CHECK(options({}).port == 29810 && !options({}).diagnostics);
    CHECK(options({"--diagnostics"}).diagnostics);
    CHECK(options({"12345", "--diagnostics"}).port == 12345);
    CHECK(options({"--diagnostics", "12345"}).diagnostics);
    for (auto arguments : {std::vector<std::string_view>{"--diagnostic-slam-query"},
                           {"--diagnostics", "--diagnostics"}, {"-1"}, {"0"}, {"65536"},
                           {"123abc"}, {"+29810"}, {"1", "2"}, {""}, {"--DIAGNOSTICS"}}) {
        bool rejected = false;
        try { (void)vut::parseServiceOptions(arguments); } catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
    std::cout << "PASS default production has no SLAM queries; diagnostic lifetime and service option boundaries\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
