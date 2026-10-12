#include "vut/setup_configuration.hpp"
#include <iostream>
int main() try {
    const auto root=std::filesystem::temp_directory_path()/vut::frontend::installationUuid();
    std::filesystem::create_directories(root);const auto path=root/"service.conf";
    vut::frontend::saveMap(path,"01:02:03:04:05:06","room-map");
    const auto first=vut::frontend::loadConfiguration(path);
    if(first.source.empty()||first.maps.at("01:02:03:04:05:06")!="room-map")throw std::runtime_error("configuration not saved");
    vut::frontend::saveMap(path,"06:05:04:03:02:01","other-map");
    const auto second=vut::frontend::loadConfiguration(path);
    if(first.source!=second.source||second.maps.size()!=2)throw std::runtime_error("installation identity or other map lost");
    try{vut::frontend::saveMap(path,"slot-1","Left Foot");throw std::logic_error("invalid mapping accepted");}catch(const std::invalid_argument&){}
    {std::ofstream stream(path);stream<<"invalid existing data\n";}
    try{vut::frontend::saveMap(path,"01:02:03:04:05:06","map");throw std::logic_error("corrupt configuration replaced");}catch(const std::runtime_error& e){if(std::string(e.what())=="corrupt configuration replaced")throw;}
    std::ifstream input(path);std::string line;std::getline(input,line);if(line!="invalid existing data")throw std::runtime_error("original was not preserved");
    input.close();std::filesystem::remove(path);std::filesystem::remove(root);std::cout<<"PASS atomic explicit RF/map config, installation identity preservation, invalid/corrupt rejection\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
