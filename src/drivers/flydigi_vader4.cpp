// FlyDigi Vader 4 Pro (and the Apex 4 / Vader 3 Pro family) on vendor id 04B4, product id 2412.
//
// IMPORTANT: the controller only exposes this vendor collection while it is in DInput mode
// (hold FN + A until the LED turns blue). In XInput mode it enumerates as 045E:028E — a plain
// Xbox 360 gamepad with no vendor collection and no battery field at all, which is why the
// XInput path in xinput.cpp can only ever report four coarse levels for it.
//
// Protocol: write a 12-byte output report 0x05 whose second byte is 0xEC (device info, read-only).
// The reply is not a report of its own: it arrives inside the 0x04 input stream and is identified
// by the command echo. Byte 11 of the reply is the level 0..5 (6 = charging), so the granularity is
// 20 %, not 1 %.
//
// Taken from PeriTray (github.com/oneday5799/PeriTray, src-tauri/src/wireless_24g/drivers/flydigi/
// vader4pro.rs), which in turn transcribes Jackwmtr/flydigi-apex4-linux docs/PROTOCOL.md
// ("battery level 0..5, with 6 meaning charging") and dantmnf/Vader4ProReader.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x04B4;
constexpr USHORT kPid = 0x2412;

constexpr BYTE kCmdDeviceInfo = 0xEC;
constexpr BYTE kLevelCharging = 6;
constexpr int kLevelMax = 5;

}  // namespace

void PollFlydigiVader4(PollContext& ctx)
{
    if (ctx.IsClaimed(kVid, kPid))
        return;
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
        int level = -1;

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
            // Numbered replies keep the 0x04 report id in byte 0; unnumbered ones shift left by one.
            const size_t echo = (r.size() > 0 && r[0] == 0x04) ? 16 : 15;
            if (r.size() <= echo || r[echo] != kCmdDeviceInfo)
                continue;
            level = r[echo - 5];  // reply byte 11, counted from the command echo
            break;
        }

        if (level < 0)
            continue;
        if (level == kLevelCharging)
            continue;  // charging: the pad reports no level, leave it to the XInput path
        if (level > kLevelMax)
            continue;

        DeviceStatus s;
        s.key = L"flydigi:" + Hex4(kPid);
        s.name = L"FlyDigi Vader 4 Pro";
        s.via = L"2.4 GHz receiver";
        s.kind = DeviceKind::Gamepad;
        s.percent = Clamp100(level * 100 / kLevelMax);
        s.charging = false;
        s.vid = kVid;
        s.levelText = std::to_wstring(level) + L"/" + std::to_wstring(kLevelMax);
        ctx.Add(s, kVid, kPid);
        break;
    }
}
