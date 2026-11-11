# Changelog

## Unreleased

- Align the ESP-IDF application descriptor with runtime version 0.1.3 and check both version declarations in host tests. The version-aligned rebuild requires its own physical-device acceptance.

- Keep repeated SSD1677 partial refreshes synchronized with standby/resume power state, including tutorial page navigation.
- Cover six consecutive SSD1677 standby/partial cycles in the actual-driver regression suite.

- Route display readback through bidirectional SDA on GPIO14 at 2 MHz, matching SSD2677 read timing while preserving the shared MicroSD return line.
- Add a board SPI configuration regression test and boot diagnostics for readback setup.

## 0.1.3 - 2026-11-11

- Automatically detect SSD1677 and SSD2677 at startup and use one firmware for both screen revisions.
- Add SSD2677 full, partial and four-level grayscale refresh, temperature waveforms, previous-frame tracking and sleep support.
- Enable half-duplex SPI reads, log the selected controller and enter standby between refreshes.
- Add host driver regression tests and document the required two-revision hardware validation.
- Match Playground release flashing with DIO, 80 MHz and 32 MB flash.

## 0.1.0 - 2026-08-26

首个可发布版本，包含：

- 成长型桌宠系统：孵化、命名、性格、成长、亲密度、饱腹值、能量、互动、睡眠、外出、连续陪伴成就和NVS存档。
- 番茄钟：15、25、60分钟预设，自定义时间、暂停、继续、结束确认、完整倒计时刷新和到时提示音。
- 状态板：`BUSY`、`MEETING`、`ON CALL`、`OPEN TO TALK`、`REST`和`CUSTOM`，支持横屏展示和自定义键盘。
- 答案书：信息答案与YES/NO/尚不明确两种模式，持续摇晃检测、思考动画和350条信息答案。
- 应用选择器：AI键与上下滑动开关、触摸选择、IMU旋转选择、摇晃选择和双击返回桌宠。
- 低功耗：双侧键主动休眠、按计划RTC唤醒、桌宠自主事件、电池电量和充电状态显示。
- 六页首次开机教程：桌宠、成长、应用、选择器、旋转和摇晃操作说明，支持桌宠页面重新打开。
