#pragma once
#include "monaka/protocol/v2/codec.hpp"
#include <map>
#include <set>
#include <string_view>

// Generic test consumer: only C1 models/codec, no VIVE headers or linkage.
class MockConsumer {
public:
    struct Cache {
        std::int64_t poseSequence=-1, stateSequence=-1, freshAt=-1;
        std::uint32_t revision=0;
        bool present=true;
    };
    bool receive(std::string_view bytes, std::int64_t now);
    bool fresh(const std::string& source,const std::string& device,std::int64_t now) const;
    const Cache& cache(const std::string& source,const std::string& device) const;
private:
    struct Source { std::string session; std::set<std::string> retired; std::map<std::string,Cache> devices; };
    std::map<std::string,Source> sources_;
};
