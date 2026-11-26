# PMOKEY WAVE 75 (WOBKEY) on the 36B0:3002 receiver

## Scope and hardware validation

This driver covers the PMOKEY WAVE 75 keyboard paired with its 2.4 GHz receiver
`36B0:3002`. Only the receiver's raw-HID collection is probed: usage page
`FF60`, usage `0x61`, input and output report length at least 10 and 3 bytes.
Other PMOKEY/WOBKEY models, and the same keyboard over Bluetooth or USB cable,
are deliberately not touched: the receiver is the only identity we can match
without guessing, and Bluetooth is already covered by the generic Windows
Bluetooth battery path.

The keyboard was tested on Windows 11 while paired over the receiver:

```
PMOKEY WAVE 75  [2.4 GHz receiver]  84%  (pmo:3002)
```

The number matched what PMO Hub showed for the same keyboard at the same
moment. Charging state was observed both plugged in (byte 9 non-zero) and on
battery (byte 9 zero).

## Protocol

Three read-only steps, in this order. Every frame is an output report whose
first byte is the report id `0x00`:

```
START   AA 10        ->  AA 10 .. 55
QUERY   AA 30 18     ->  AA 30 18 .. 55 <level> <charging>
STOP    AA 11        ->  AA 11 .. 55
```

* `0x55` in byte 7 marks a successful reply; anything else is a failure and the
  reply is skipped.
* `18` is a fixed parameter (24), not a length.
* The query must be preceded by START. Without it the firmware answers `0x0F`
  (failure) no matter how often the query is repeated.
* Byte 8 of the query reply is the level 0..100, byte 9 is the charging flag.
* STOP is always sent, including when the query failed, so the keyboard does
  not stay in command mode.

Timing: 300 ms for START, up to 700 ms for each of at most three QUERY
attempts. Reads use a single absolute deadline per step, so unrelated traffic
on the same collection cannot extend the wait.

## Where the protocol comes from

PMO Hub 2.1.7 (Electron). The constants `CMD_START_COMM 0x10`,
`CMD_STOP_COMM 0x11` and `CMD_GET_BATTERY_STATUS 0x30`, together with the reply
layout, live in its `app.asar`; the three-step conversation and the byte
positions were then confirmed against the hardware with a raw-HID probe before
this driver was written.

## Limitations

* Only the receiver is matched. A keyboard connected over Bluetooth shows up
  through the generic Bluetooth path (Level 1) when Windows exposes its battery.
* If PMO Hub is running and holds the collection exclusively, the driver skips
  the keyboard instead of retrying forever: the query needs a writable handle,
  and a read-only handle can never send it.
