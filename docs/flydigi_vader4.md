# FlyDigi Vader 4 Pro (and the 04B4:2412 DInput family)

## Scope and hardware validation

This driver covers FlyDigi pads that report the vendor id `04B4` with product
id `2412`. That product id is the DInput-mode identity of the Vader 4 Pro and,
per the protocol sources below, of the Apex 4 and Vader 3 Pro family as well;
the driver therefore calls them by the family name rather than inventing a
model. In XInput mode the same pad only exists as `045E:028E`, which exposes no
vendor collection at all.

Two independent sources are tried in order. Both only read: no configuration,
lighting or firmware command is ever sent.

### 1. FlyDigi Space Station service pipe (preferred)

`SpaceStationService.exe` exposes `\\.\pipe\fcs.sock` and already owns the pad,
so this path works in XInput *and* DInput mode.

```
request : "FDG_PROTOCOL\n" + int32le(payload length) + protobuf(IpcCommand)
          IpcCommand{ cmdId = 4097 }  ->  08 81 20 1A 00
reply   : "FDG_PROTOCOL\n" + int32le(length) + protobuf(IpcResult)
          IpcResult.field5 -> repeated DeviceInfo
          DeviceInfo.field3 = connectType (1 wired, 2 dongle, 3 bluetooth)
          DeviceInfo.field9 = battery level 0..6
```

The reply typically arrives in 4-5 ms. The driver polls with `PeekNamedPipe`
under one absolute 800 ms deadline and gives up after two 40 ms retries if the
pipe cannot be opened.

`field9` is a step count, not a percentage: the pad only knows 0..6, so the
percentage is derived (`level * 100 / 6`) and the raw step is kept in the
detail text so the rounding is visible.

The service reports the connection type, not whether the battery is charging; a
wired pad is not necessarily charging, so charging is left unknown (`false`)
instead of being inferred from the connection.

### 2. Vendor HID collection (DInput mode only)

When the service is not running or the pipe is held by the Space Station app,
the driver falls back to the vendor collection `0xFFA0`, which only exists in
DInput mode (hold FN + A until the LED turns blue).

```
write : 12-byte output report 05 EC 00 00 00 00 00 00 00 00 00 00   (device info)
read  : 0x04 input stream, byte 15 == 0xEC (command echo), byte 11 = level 0..5
        6 means charging, in which case no level is reported
```

One absolute 1000 ms deadline; a numbered reply keeps the report id in byte 0,
an unnumbered one shifts the echo and the level one byte left, which is why the
driver computes the offset from the presence of `0x04`.

## XInput identity

In XInput mode Windows reports `045E:028E` and the generic path can only invent
a fixed "100 %" for it. After either source above succeeds, the driver claims
`045E:028E` as well so `PollWgi` skips it and no second, wrong entry appears.

## Connection modes (measured on hardware)

The same pad was dumped through the pipe in all three connection modes. The
reply has the same shape in every mode; only `field3` and one entry of the
component-version list change.

| Connection | HID identity | `field3` | `field9` | `field11` | `field12` |
|---|---|---|---|---|---|
| USB cable | `045E:028E` | 1 | 4 | 1118 | 654 |
| 2.4 GHz receiver | `045E:028E` | 2 | 4 | 1118 | 654 |
| Bluetooth | `D7D7:0041` (BLE) | *pipe returns an empty device list* | — | — | — |

Two findings worth knowing before building anything on top of this:

* **There is no charging field.** `DeviceInfo` carries 13 fields (name,
  category, connectType, device code, MAC prefix, serial, isConnected, battery,
  firmware, two constants, component versions) and none of them changes when
  the cable is plugged in. `field11` and `field12` are `1118` and `654` in both
  the wired and the wireless dump, so they do not encode supply or charging
  either.
* **Bluetooth is not reachable through either path.** In Bluetooth mode the pad
  becomes a BLE device (`D7D7:0041`, PnP node `BTHLE\DEV_A4C138901DC2`, MAC
  matching `field6` of the wired dump) that exposes only a standard gamepad
  collection (usage page 1) and no `0xFFA0`. The service does not enumerate it:
  `GetConnectedDevices` answers `08 81 20 10 01 2a 00`, i.e. `field5` with
  length 0. Windows also exposes no `DEVPKEY_Device_BatteryLevel` for the node.
  So `connectType == 3` is currently unreachable, and no level can be produced
  in Bluetooth mode.

### Dumping the pipe reply yourself

Useful when adding another FlyDigi model, or when a firmware update changes the
reply:

```
write : b"FDG_PROTOCOL\n" + struct.pack("<i", 5) + bytes([0x08, 0x81, 0x20, 0x1A, 0x00])
read  : "FDG_PROTOCOL\n" + int32le(len) + protobuf(IpcResult)
                   (or "FDG_COMPRESSED" + gzip, or "FDG_CHUNK" + fragments)
walk  : IpcResult.field5 -> repeated field1 (DeviceInfo)
```

Every field is either a varint or length-delimited, no packed repeated fields,
so a ~40 line recursive reader is enough. Comparing the tree across the three
connection modes is how the table above was produced.

## Where the protocol comes from

* Space Station pipe: <https://github.com/wutong233/flydigi-battery>
  (`docs/protocol.md`).
* DInput collection: <https://github.com/oneday5799/PeriTray> and
  <https://github.com/Jackwmtr/flydigi-apex4-linux>.

Both layouts were confirmed against a real Vader 4 Pro on Windows 11; the
recorded outputs were:

```
FlyDigi Vader 4 Pro  [2.4 GHz receiver]  60%  (3/5 steps)  (flydigi:2412)
FlyDigi Vader 4 Pro  [USB cable]         80%  (4/5 steps)  (flydigi:2412)
```

(`3/5` and `4/5` are the step counts the pad itself reports; the percentage is
derived.)

## Limitations

* The pipe lists the whole FlyDigi family and carries no product name, so every
  device found through it is labelled "FlyDigi Vader 4 Pro".
* Bluetooth mode is not covered by either path: the service does not enumerate
  the pad there and the DInput collection only exists in DInput mode. The pad
  can still be reported by the generic XInput path, but without a real level.
* Charging state is unknown on both paths; the DInput collection only reports
  it as a level value of `6`.
* If neither the service nor the DInput collection is available, the pad falls
  back to the generic XInput path and shows a coarse level.
