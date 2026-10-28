#include "vut/agent_lifecycle.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace vut;
namespace fs = std::filesystem;
#define CHECK(x) do { if(!(x)) throw std::runtime_error("CHECK failed: " #x); } while(false)
struct FakeRuntime : AgentRuntime { bool start() override{return true;} bool pollOnce() override{return true;} bool shutdown() noexcept override{return true;} };
LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(h,m,w,l);
}
int windowChild(const fs::path& root) {
 WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"SA04SyntheticWindow";wc.lpfnWndProc=procedure;
 CHECK(RegisterClassW(&wc));
 auto main=CreateWindowW(wc.lpszClassName,L"SA04 synthetic main",WS_OVERLAPPEDWINDOW,100,100,320,150,nullptr,nullptr,wc.hInstance,nullptr);CHECK(main);
 ShowWindow(main,SW_SHOWMINIMIZED);
 // Newest first in enumeration: this hidden top-level must never be selected.
 auto helper=CreateWindowW(wc.lpszClassName,L"SA04 hidden helper",WS_OVERLAPPEDWINDOW,0,0,100,100,nullptr,nullptr,wc.hInstance,nullptr);CHECK(helper);
 std::ofstream(root/"windows")<<reinterpret_cast<std::uintptr_t>(main)<<' '<<reinterpret_cast<std::uintptr_t>(helper);
 SetTimer(main,1,15000,nullptr);
 MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){if(msg.message==WM_TIMER){DestroyWindow(main);break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
 if(IsWindow(helper))DestroyWindow(helper);return 0;
}
std::pair<HWND,HWND> readWindows(const fs::path& root) {
 auto end=GetTickCount64()+5000;
 while(GetTickCount64()<end){std::uintptr_t a=0,b=0;std::ifstream(root/"windows")>>a>>b;if(a&&b)return {reinterpret_cast<HWND>(a),reinterpret_cast<HWND>(b)};Sleep(10);}
 throw std::runtime_error("fixture windows unavailable");
}
int main(int argc,char** argv) try {
 CHECK(argc==3);
 const fs::path root=fs::absolute(argv[2]);
 if(std::string(argv[1])=="--child")return windowChild(root);
 CHECK(std::string(argv[1])=="--test");
 CHECK(fs::create_directories(root));
 fs::create_directory(root/"owned");fs::create_directory(root/"other");
 std::ofstream(root/"config")<<"fake";std::ofstream(root/"config-other")<<"fake";
 AgentLifecycle agent,other;
 CHECK(agent.start({root/"config",root/"journal",32001},[]{return std::make_unique<FakeRuntime>();}));
 CHECK(other.start({root/"config-other",root/"journal-other",32002},[]{return std::make_unique<FakeRuntime>();}));
 auto exe=fs::absolute(argv[0]);
 agent.spawnOwnedChild(exe,{L"--child",(root/"owned").wstring()},AgentChildVisibility::Normal);
 other.spawnOwnedChild(exe,{L"--child",(root/"other").wstring()},AgentChildVisibility::Normal);
 auto [main,helper]=readWindows(root/"owned");auto [otherMain,otherHelper]=readWindows(root/"other");
 CHECK(IsIconic(main)&&IsIconic(otherMain)&&!IsWindowVisible(helper)&&!IsWindowVisible(otherHelper));
 CHECK(agent.showOwnedChild());
 const auto end=GetTickCount64()+2000;while(IsIconic(main)&&GetTickCount64()<end)Sleep(10);
 CHECK(!IsIconic(main)&&!IsWindowVisible(helper));CHECK(IsIconic(otherMain)&&!IsWindowVisible(otherHelper));
 agent.requestStop();CHECK(!agent.showOwnedChild());agent.shutdown();CHECK(agent.state()==AgentState::Stopped);CHECK(!agent.showOwnedChild());
 CHECK(IsWindow(otherMain));other.requestStop();other.shutdown();CHECK(other.state()==AgentState::Stopped);
 std::cout<<"PASS retained owned visible main restored; hidden helper and same-executable other instance untouched; exited refused\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
