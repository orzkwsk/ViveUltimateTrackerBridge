// Offline, native GUI-subsystem console test. No USB/HID operations.
#include <windows.h>
#include <cstdio>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR commandLine, int) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    STARTUPINFOA startup{}; startup.cb=sizeof(startup); GetStartupInfoA(&startup);
    DWORD mode = 0;
    const bool before = GetConsoleMode(input, &mode) != FALSE;
    const bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (input != GetStdHandle(STD_INPUT_HANDLE)) SetStdHandle(STD_INPUT_HANDLE, input);
    const bool after = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != FALSE;
    // Private filename passed without quotes: test directory has no whitespace.
    FILE* file = nullptr;
    if (fopen_s(&file, commandLine, "w") != 0) return 33;
    std::fprintf(file, "before=%s; attach=%s; after=%s; input=%p; startupInput=%p; flags=%lu", before ? "True" : "False",
        attached ? "True" : "False", after ? "True" : "False",input,startup.hStdInput,startup.dwFlags);
    std::fclose(file);
    if (!after) return 31;
    while (true) {
        INPUT_RECORD record{}; DWORD read = 0;
        if (!ReadConsoleInputW(GetStdHandle(STD_INPUT_HANDLE), &record, 1, &read)) return 32;
        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown &&
            record.Event.KeyEvent.uChar.AsciiChar == 'q') return 23;
    }
}
