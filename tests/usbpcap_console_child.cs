// Test-only GUI-subsystem child. Mirrors the relevant console attach/input behavior,
// uses no USB/HID API, and returns 23 after receiving key-down q.
using System;
using System.IO;
using System.Runtime.InteropServices;
public class ConsoleChild {
    [StructLayout(LayoutKind.Explicit, Size=20)] public struct Record {
        [FieldOffset(0)] public ushort Type;
        [FieldOffset(4)] public int Down;
        [FieldOffset(14)] public char Character;
    }
    [DllImport("kernel32.dll")] static extern IntPtr GetStdHandle(int id);
    [DllImport("kernel32.dll")] static extern bool GetConsoleMode(IntPtr h, out uint mode);
    [DllImport("kernel32.dll")] static extern bool AttachConsole(uint id);
    [DllImport("kernel32.dll")] static extern bool SetStdHandle(int id, IntPtr h);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern bool ReadConsoleInputW(IntPtr h, out Record r, uint count, out uint read);
    public static int Main(string[] args) {
        IntPtr input=GetStdHandle(-10); uint mode;
        bool before=GetConsoleMode(input, out mode);
        bool attached=AttachConsole(0xffffffff);
        if (input != GetStdHandle(-10)) SetStdHandle(-10, input);
        bool after=GetConsoleMode(GetStdHandle(-10), out mode);
        File.WriteAllText(args[0], "before="+before+"; attach="+attached+"; after="+after);
        if (!after) return 31;
        while (true) {
            Record r; uint read;
            if (!ReadConsoleInputW(GetStdHandle(-10),out r,1,out read)) return 32;
            if (r.Type == 1 && r.Down != 0 && r.Character == 'q') return 23;
        }
    }
}
