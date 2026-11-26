<!--
标题建议：
  feat(driver): 设备名（VID:PID）      例如 feat(driver): PMOKEY WAVE 75 (36B0:3002)
  fix(tray): 一句话描述问题            例如 fix(tray): keep icons when a device-arrival pass comes back incomplete
  docs: / refactor: / chore:          其它改动

本模板同时用于「新增设备驱动」和「修复问题」，不适用的段落写“不涉及”即可。
允许使用 AI 辅助写作与编码，但请自己跑过构建并如实填写实机验证；提交信息里不要带
`Co-authored-by` 之类的 AI 署名。
-->

## What this adds / 这个 PR 做了什么

<!-- 一段话说清：解决什么问题、用户看到的变化是什么。 -->

**复现方式**（修 bug 必填）：

<!-- 例如：保持 3–4 个无线设备在列，插上一个 2.4G 接收器，图标会集体闪掉再回来。 -->

**根因**（修 bug 必填）／**设备与 ID**（新驱动必填）：

<!-- 新驱动请写：设备名、VID:PID、连接方式（USB 线 / 2.4G 接收器 / 蓝牙）、走哪个 HID 集合。 -->

### 改动 / Changes

<!-- 按文件逐条列出，并说明为什么这样改：
     - src/drivers/xxx.cpp：新增 PollXxx，...
     - src/devices.cpp：把 PollXxx 插在 PollYyy 之前，因为 ...
-->

## Where the protocol comes from / 协议来源

<!-- 新驱动必填：厂商软件名与版本、网页驱动 URL、开源实现链接，或自己抓的 pcap（说明抓法）。
     同时写出关键帧格式：请求与回复的字节、电量字段位置、充电字段、成功判定。
     只改枚举/状态机/界面、不涉及新协议的，写“不涉及新协议”。 -->

## Tested on real hardware? / 实机验证

<!-- 必填。请给出：
     - 设备型号与连接方式；
     - `WirelessBattery.exe --list` 的原始输出（并与厂商软件显示的数值对比）；
     - 修复前后的行为差异，或新旧版本对比；
     - 没有实机就如实写 No，并说明依据（抓包、上游代码、他人实测等）。 -->

## Checklist

- [ ] Only read-only battery queries are sent; no settings, lighting or firmware are changed
- [ ] Only devices recognised by vendor/product ID (or a protocol-specific collection) are touched
- [ ] No network access, no launching other programs, no new files outside `src/`, `docs/` or `README.md`
- [ ] No binaries or generated files are included
- [ ] No `Co-authored-by` lines crediting AI tools in the commit messages
- [ ] Changes to `.github/`, `installer/`, `build.bat` or `Makefile` are explained above
- [ ] 本地构建通过（`make` 或 `build.bat`），`make test` 无回归；或说明未构建的原因
