// PMOKEY WAVE 75 (WOBKEY) on its 2.4 GHz receiver 36B0:3002.
//
// The receiver exposes a raw-HID collection (usage page FF60, usage 61, 32-byte reports).
// Battery is asked for with the vendor's three-step conversation, all read-only:
//
//   START  AA 10              -> AA 10 .. 55            (0x55 = success flag)
//   QUERY  AA 30 18           -> AA 30 18 .. 55 <level> <charging>
//   STOP   AA 11              -> AA 11 .. 55
//
// `18` is a fixed parameter (24), not a length. The query must be preceded by START, otherwise
// the firmware answers 0x0F (failure) — that detail cost a long afternoon.
//
// Values in the reply: byte 7 = 0x55 on success, byte 8 = level 0..100, byte 9 = charging.
// Protocol taken from PMO Hub 2.1.7 (Electron; the constants CMD_START_COMM 0x10 /
// CMD_STOP_COMM 0x11 / CMD_GET_BATTERY_STATUS 0x30 and the reply layout live in app.asar),
// confirmed against the keyboard itself with a raw-HID probe before this driver was written.
#include "drivers.h"

namespace {

constexpr USHORT kVid = 0x36B0;
constexpr USHORT kPid = 0x3002;

// Waits for one reply carrying `command` and the success flag. Reports from other traffic on the
// same collection are skipped without restarting the deadline.
bool AwaitReply(HidDev& d, BYTE command, std::vector<BYTE>& reply, int timeoutMs)
{
    const ULONGLONG end = GetTickCount64() + (ULONGLONG)timeoutMs;
    while (true) {
        const ULONGLONG now = GetTickCount64();
        if (now >= end)
            return false;
        const int remaining = (int)(end - now);
        if (d.Read(reply, remaining) < 0)
            continue;  // timeout slice: keep waiting until the absolute deadline
        if (reply.size() >= 10 && reply[0] == 0xAA && reply[1] == command && reply[7] == 0x55)
            return true;
    }
}

}  // namespace

void PollPmoWave75(PollContext& ctx)
{
    if (ctx.IsClaimed(kVid, kPid))
        return;
    for (const HidColl* c : ctx.Find(kVid, kPid, [](const HidColl& h) {
             return h.page() == 0xFF60 && h.usage() == 0x61 && h.inLen() >= 10 && h.outLen() >= 3;
         })) {
        HidDev d;
        if (!d.Open(*c))
        {
            // Another application (PMO Hub) may hold the collection; fall back to a read handle.
            if (!d.OpenReadOnly(*c))
                continue;
        }
        d.Flush();

        std::vector<BYTE> reply;
        if (!d.Write({ 0x00, 0xAA, 0x10 }))
            continue;  // START_COMM
        AwaitReply(d, 0x10, reply, 300);

        bool ok = false;
        for (int attempt = 0; attempt < 3 && !ok; attempt++)
        {
            if (!d.Write({ 0x00, 0xAA, 0x30, 0x18 }))
                break;  // GET_BATTERY_STATUS
            ok = AwaitReply(d, 0x30, reply, 700);
        }

        d.Write({ 0x00, 0xAA, 0x11 });  // STOP_COMM: never leave the keyboard in command mode

        if (!ok || reply.size() < 10)
            continue;

        DeviceStatus s;
        s.key = L"pmo:" + Hex4(kPid);
        s.name = L"PMOKEY WAVE 75";
        s.via = L"2.4 GHz receiver";
        s.kind = DeviceKind::Keyboard;
        s.percent = Clamp100(reply[8]);
        s.charging = reply[9] != 0;
        s.vid = kVid;
        ctx.Add(s, kVid, kPid);
        break;
    }
}
