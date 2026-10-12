#pragma once
#include <algorithm>
#include <cwctype>
#include <windows.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace vut::frontend {
// Compatibility admission config, never a stable-device/body-role store.
struct SetupConfiguration {
    std::string source;
    std::map<std::string,std::string> maps;
    std::string reconnectRevision;
};
inline bool configToken(const std::string& value) {
    return !value.empty() && value.size()<=96 && std::none_of(value.begin(),value.end(),[](unsigned char c){return c<=32 || c==127;});
}
inline bool rfAddress(const std::string& value) {
    if(value.size()!=17)return false;
    for(std::size_t i=0;i<value.size();++i)if(i%3==2 ? value[i]!=':' : !(value[i]>='0'&&value[i]<='9')&&!(value[i]>='a'&&value[i]<='f'))return false;
    return true;
}
inline SetupConfiguration loadConfiguration(const std::filesystem::path& path) {
    SetupConfiguration result;
    if(!std::filesystem::exists(path))return result;
    if(std::filesystem::file_size(path)>1024*1024)throw std::runtime_error("Configuration exceeds its size limit; original file preserved.");
    std::ifstream stream(path);if(!stream)throw std::runtime_error("Configuration cannot be read; original file was preserved.");
    std::string line;
    while(std::getline(stream,line)) {
        std::istringstream row(line);std::string key,a,b,extra;
        if(!(row>>key)||key[0]=='#')continue;
        if(!(row>>a))throw std::runtime_error("Incomplete configuration; original file was preserved.");
        if(key=="source"&&result.source.empty()&&configToken(a))result.source=a;
        else if(key=="map"&&(row>>b)&&rfAddress(a)&&configToken(b)) {if(!result.maps.emplace(a,b).second)throw std::runtime_error("Duplicate map; original configuration preserved.");}
        else if(key=="reconnect_revision"&&result.reconnectRevision.empty()&&(a=="strict"||a=="preserve"))result.reconnectRevision=a;
        else throw std::runtime_error("Invalid configuration; original file was preserved.");
        if(row>>extra)throw std::runtime_error("Unexpected configuration field; original file was preserved.");
    }
    if(result.source.empty())throw std::runtime_error("Existing configuration lacks a source identity; original file was preserved.");
    return result;
}
inline std::string installationUuid() {
    GUID value{};if(FAILED(CoCreateGuid(&value)))throw std::runtime_error("Installation identity could not be created.");
    wchar_t text[40]{};if(!StringFromGUID2(value,text,40))throw std::runtime_error("Installation identity could not be formatted.");
    std::string result;for(int i=1;i<37;++i)result+=static_cast<char>(std::towlower(text[i]));return result;
}
inline void saveMap(const std::filesystem::path& path,const std::string& rf,const std::string& map) {
    if(!rfAddress(rf)||!configToken(map))throw std::invalid_argument("Select an observed RF address and a map ID (1..96 bytes, no whitespace).");
    auto configuration=loadConfiguration(path);
    if(configuration.source.empty())configuration.source="vive-"+installationUuid();
    configuration.maps[rf]=map;
    std::ostringstream content;content<<"# Explicit observed RF admission; RF is not stable hardware identity.\nsource "<<configuration.source<<'\n';
    for(const auto& [address,id]:configuration.maps)content<<"map "<<address<<' '<<id<<'\n';
    if(!configuration.reconnectRevision.empty())content<<"reconnect_revision "<<configuration.reconnectRevision<<'\n';
    std::filesystem::create_directories(path.parent_path());
    auto temporary=path;temporary+=L"."+std::filesystem::path(installationUuid()).wstring()+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Configuration save failed; check folder permissions. Original file preserved.");
    auto bytes=content.str();DWORD count=0;
    const bool written=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size()&&FlushFileBuffers(file);
    CloseHandle(file);
    if(!written||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;std::filesystem::remove(temporary,ignored);
        throw std::runtime_error("Atomic configuration save failed; original file was preserved.");
    }
}
} // namespace vut::frontend
