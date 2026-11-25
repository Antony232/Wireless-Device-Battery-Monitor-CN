// FlyDigi Vader 4 Pro (also Apex 4 / Vader 3 Pro family) on vendor id 04B4, product id 2412.
//
// Two independent sources, tried in this order.
//
// 1. The Flydigi Space Station background service pipe \\.\pipe\fcs.sock.
//    The service already owns the pad and exposes the same 0..6 battery level the official UI
//    shows, so this works whether the pad is in XInput or in DInput mode.
//    Transport:  "FDG_PROTOCOL\n" + int32le(len) + protobuf(IpcCommand{cmdId = 4097})
//    Reply:      protobuf IpcResult -> field 5 -> repeated field 1 (DeviceInfo)
//                DeviceInfo: field 3 = connectType (1 wired, 2 dongle, 3 bluetooth)
//                            field 9 = battery level 0..6
//    Layout transcribed from wutong233/flydigi-battery docs/protocol.md.
//
// 2. The vendor HID collection 0xFFA0, which only exists in DInput mode (hold FN + A until the LED
//    turns blue). Write a 12-byte output report 0x05 whose second byte is 0xEC (device info,
//    read-only); the reply arrives inside the 0x04 input stream, byte 11 = level 0..5 (6 = charging).
//    Taken from PeriTray (github.com/oneday5799/PeriTray) and Jackwmtr/flydigi-apex4-linux.
//
// The XInput identity (045E:028E) exposes no vendor collection at all - that is why the generic
// XInput paths can only ever invent a "100 %" for this pad. Once the pipe gives us the real level we
// claim that identity as well, so PollWgi does not add a second, wrong entry for the same device.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x04B4;
constexpr USHORT kPid = 0x2412;
constexpr USHORT kXinputVid = 0x045E;
constexpr USHORT kXinputPid = 0x028E;

constexpr BYTE kCmdDeviceInfo = 0xEC;
constexpr BYTE kLevelCharging = 6;
constexpr int kLevelMax = 5;        // DInput vendor protocol: 0..5, 6 = charging
constexpr int kPipeLevelMax = 6;    // service pipe: 0..6, 7 steps
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\fcs.sock";

// ---- minimal protobuf reader -------------------------------------------------------------

struct PbField {
    bool ok = false;
    int num = 0;
    int wire = 0;
    ULONGLONG v = 0;
    size_t off = 0;
    size_t len = 0;
};

bool ReadVarint(const std::vector<BYTE>& b, size_t& i, size_t end, ULONGLONG& out)
{
    out = 0;
    int shift = 0;
    while (i < end)
    {
        const BYTE c = b[i++];
        out |= (ULONGLONG)(c & 0x7F) << shift;
        if (!(c & 0x80))
            return true;
        shift += 7;
        if (shift > 63)
            return false;
    }
    return false;
}

PbField NextField(const std::vector<BYTE>& b, size_t& i, size_t end)
{
    PbField f;
    if (i >= end)
        return f;
    ULONGLONG key = 0;
    if (!ReadVarint(b, i, end, key))
        return f;
    f.num = (int)(key >> 3);
    f.wire = (int)(key & 7);
    if (f.wire == 0)
    {
        if (!ReadVarint(b, i, end, f.v))
            return PbField{};
    }
    else if (f.wire == 2)
    {
        ULONGLONG len = 0;
        if (!ReadVarint(b, i, end, len) || len > (ULONGLONG)(end - i))
            return PbField{};
        f.off = i;
        f.len = (size_t)len;
        i += (size_t)len;
    }
    else if (f.wire == 5)
    {
        if (end - i < 4)
            return PbField{};
        i += 4;
    }
    else if (f.wire == 1)
    {
        if (end - i < 8)
            return PbField{};
        i += 8;
    }
    else
    {
        return PbField{};
    }
    f.ok = true;
    return f;
}

// ---- service pipe ------------------------------------------------------------------------

#ifdef _WIN32

// Opens the pipe, asks for the connected device list and pulls the first gamepad's battery level.
bool ReadServiceLevel(int& level, int& connectType)
{
    HANDLE h = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 2 && h == INVALID_HANDLE_VALUE; attempt++)
    {
        h = CreateFileW(kPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            Sleep(40);  // the app occasionally holds the single pipe instance for a moment
    }
    if (h == INVALID_HANDLE_VALUE)
        return false;

    static const BYTE kReq[] = { 0x08, 0x81, 0x20, 0x1A, 0x00 };  // IpcCommand{ cmdId = 4097 }
    static const char kHead[] = "FDG_PROTOCOL\n";

    std::vector<BYTE> frame(kHead, kHead + (sizeof(kHead) - 1));
    const DWORD n = (DWORD)sizeof(kReq);
    frame.push_back((BYTE)(n & 0xFF));
    frame.push_back((BYTE)((n >> 8) & 0xFF));
    frame.push_back((BYTE)((n >> 16) & 0xFF));
    frame.push_back((BYTE)((n >> 24) & 0xFF));
    frame.insert(frame.end(), kReq, kReq + sizeof(kReq));

    DWORD written = 0;
    const BOOL sent = WriteFile(h, frame.data(), (DWORD)frame.size(), &written, nullptr);

    std::vector<BYTE> buf;
    const ULONGLONG deadline = GetTickCount64() + 800;
    bool done = false;

    while (sent && !done && GetTickCount64() < deadline)
    {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr))
            break;
        if (avail == 0)
        {
            Sleep(5);
            continue;
        }
        const size_t old = buf.size();
        buf.resize(old + avail);
        DWORD got = 0;
        if (!ReadFile(h, buf.data() + old, avail, &got, nullptr))
        {
            buf.resize(old);
            break;
        }
        buf.resize(old + got);

        size_t nl = 0;
        while (nl < buf.size() && nl < 64 && buf[nl] != '\n')
            nl++;
        if (nl >= buf.size() || nl >= 64)
            continue;  // header not complete yet
        if (std::string((const char*)buf.data(), nl) != "FDG_PROTOCOL")
            break;  // compressed / chunked: not what this command returns
        if (buf.size() < nl + 5)
            continue;
        const int len = (int)((unsigned)buf[nl + 1] | ((unsigned)buf[nl + 2] << 8) |
                              ((unsigned)buf[nl + 3] << 16) | ((unsigned)buf[nl + 4] << 24));
        if (len <= 0 || len > (32 << 20))
            break;
        if (buf.size() < nl + 5 + (size_t)len)
            continue;

        std::vector<BYTE> payload(buf.begin() + nl + 5, buf.begin() + nl + 5 + len);

        // top level: field 5 carries the device list
        size_t i = 0, listOff = 0, listLen = 0;
        bool haveList = false;
        while (i < payload.size())
        {
            const PbField f = NextField(payload, i, payload.size());
            if (!f.ok)
                break;
            if (f.num == 5 && f.wire == 2)
            {
                listOff = f.off;
                listLen = f.len;
                haveList = true;
                break;
            }
        }
        if (!haveList)
            break;

        std::vector<BYTE> list(payload.begin() + listOff, payload.begin() + listOff + listLen);
        size_t j = 0;
        while (j < list.size())
        {
            const PbField g = NextField(list, j, list.size());
            if (!g.ok)
                break;
            if (g.num != 1 || g.wire != 2)
                continue;

            std::vector<BYTE> dev(list.begin() + g.off, list.begin() + g.off + g.len);
            int category = -1, lvl = -1, conn = -1;
            size_t k = 0;
            while (k < dev.size())
            {
                const PbField d = NextField(dev, k, dev.size());
                if (!d.ok)
                    break;
                if (d.wire != 0)
                    continue;
                if (d.num == 2)
                    category = (int)d.v;
                else if (d.num == 3)
                    conn = (int)d.v;
                else if (d.num == 9)
                    lvl = (int)d.v;
            }
            if (category == 1 && lvl >= 0 && lvl <= kPipeLevelMax)
            {
                level = lvl;
                connectType = conn;
                done = true;
            }
            break;  // first gamepad only
        }
    }

    CloseHandle(h);
    return done;
}

#else  // !_WIN32

// The Flydigi service pipe is Windows only; the vendor HID path below still works.
bool ReadServiceLevel(int&, int&) { return false; }

#endif

}  // namespace

void PollFlydigiVader4(PollContext& ctx)
{
    if (ctx.IsClaimed(kVid, kPid))
        return;

    // 1. official service pipe: 7 levels, mode independent
    int level = -1;
    int connectType = -1;
    if (ReadServiceLevel(level, connectType))
    {
        DeviceStatus s;
        s.key = L"flydigi:" + Hex4(kPid);
        s.name = L"FlyDigi Vader 4 Pro";
        s.via = connectType == 1 ? L"USB cable" : L"2.4 GHz receiver";
        s.kind = DeviceKind::Gamepad;
        s.percent = Clamp100(level * 100 / kPipeLevelMax);
        s.charging = connectType == 1;
        s.vid = kVid;
        // Keep the raw step visible: the pad only knows 0..6, the percentage is derived.
        s.detail = std::to_wstring(level) + L"/" + std::to_wstring(kPipeLevelMax) + L" levels";
        ctx.Add(s, kVid, kPid);
        // Same physical pad is exposed as 045E:028E in XInput mode; without this the generic
        // Xbox path would add a second entry with a made-up 100 %.
        ctx.Claim(kXinputVid, kXinputPid);
        return;
    }

    // 2. DInput vendor collection
    for (const HidColl* c : ctx.Find(kVid, kPid, [](const HidColl& h) { return h.page() == 0xFFA0; }))
    {
        HidDev d;
        if (!d.Open(*c))
            continue;
        d.Flush();

        // Report 0x05, command 0xEC, ten zero bytes.
        if (!d.Write({ 0x05, 0xEC, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }))
            continue;

        const ULONGLONG end = GetTickCount64() + 1000;  // single absolute deadline
        std::vector<BYTE> r;
        int lvl = -1;

        while (true)
        {
            const ULONGLONG now = GetTickCount64();
            if (now >= end)
                break;
            const int remaining = (int)(end - now);
            if (remaining <= 0)
                break;
            if (d.Read(r, remaining) < 0)
                continue;
            // Numbered replies keep the 0x04 report id in byte 0, so the command echo sits at 15
            // and the level at 11. An unnumbered reply shifts both left by one.
            const size_t echo = (r.size() > 0 && r[0] == 0x04) ? 15 : 14;
            if (r.size() <= echo || r[echo] != kCmdDeviceInfo)
                continue;
            lvl = r[echo - 4];  // reply byte 11 (battery level), counted from the echo
            break;
        }

        if (lvl < 0)
            continue;
        if (lvl == kLevelCharging)
            continue;  // charging: the pad reports no level, leave it to the other paths
        if (lvl > kLevelMax)
            continue;

        DeviceStatus s;
        s.key = L"flydigi:" + Hex4(kPid);
        s.name = L"FlyDigi Vader 4 Pro";
        s.via = L"2.4 GHz receiver";
        s.kind = DeviceKind::Gamepad;
        s.percent = Clamp100(lvl * 100 / kLevelMax);
        s.charging = false;
        s.vid = kVid;
        s.detail = std::to_wstring(lvl) + L"/" + std::to_wstring(kLevelMax) + L" steps";
        ctx.Add(s, kVid, kPid);
        ctx.Claim(kXinputVid, kXinputPid);
        break;
    }
}
