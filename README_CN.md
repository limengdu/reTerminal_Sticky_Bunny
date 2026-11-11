# reTerminal Sticky Bunny

<p align="center">
  <strong>为 reTerminal Sticky 打造的成长型桌宠与交互式电子纸应用合集。</strong>
</p>

<p align="center">
  <a href="README.md">English</a> ·
  <a href="README_JA.md">日本語</a> ·
  <a href="https://www.seeedstudio.com/sticky/">Sticky 官网</a> ·
  <a href="https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html">商品详情</a> ·
  <a href="#快速开始">快速开始</a> ·
  <a href="#完整视觉导览">视觉导览</a> ·
  <a href="CHANGELOG.md">更新日志</a>
</p>

<p align="center">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/MCU-ESP32--S3-000000?style=flat-square">
  <img alt="ESP-IDF 5.4.1" src="https://img.shields.io/badge/ESP--IDF-5.4.1-000000?style=flat-square">
  <img alt="PlatformIO" src="https://img.shields.io/badge/build-PlatformIO-000000?style=flat-square">
  <img alt="Firmware 0.1.0" src="https://img.shields.io/badge/firmware-0.1.0-000000?style=flat-square">
  <img alt="MIT License" src="https://img.shields.io/badge/license-MIT-000000?style=flat-square">
  <a href="https://github.com/limengdu/reTerminal_Sticky_Bunny/actions/workflows/build.yml"><img alt="Build and test" src="https://github.com/limengdu/reTerminal_Sticky_Bunny/actions/workflows/build.yml/badge.svg"></a>
</p>

<p align="center">
  <img src="docs/images/desktop-pet/home.png" alt="Sticky Bunny 桌宠主页" width="245">
  &nbsp;&nbsp;
  <img src="docs/images/launcher/launcher-portrait.png" alt="Sticky Bunny 竖屏应用选择器" width="245">
</p>

reTerminal Sticky Bunny 把 Seeed Studio **reTerminal Sticky** 变成一个持续存在的小世界：养育一只会记住照料过程的兔子，开始一次番茄专注，展示当前状态，或者向答案书提出心中的问题。触摸、实体按键、上下滑动、设备旋转、持续摇晃、RTC 定时和电子纸低功耗策略被整合成一套连贯的固件体验。

你可以通过 [Sticky 官方网站](https://www.seeedstudio.com/sticky/) 和 [reTerminal Sticky 商品详情页](https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html)进一步了解设备。

仓库包含完整 PlatformIO/ESP-IDF 源码、原创黑白像素素材、可在电脑运行的状态与渲染测试、发布文件生成方式，以及面向用户和开发者的说明文档。

## 新旧屏幕兼容

固件 **0.1.3** 使用同一个镜像支持 Sticky 的 **SSD1677 和 SSD2677** 两种屏幕。开机读取 `0x70` 寄存器：ID 为 `0x07` 时选择 SSD2677，其他成功读取的值选择旧版 SSD1677；SPI 读取失败则停止初始化。启动日志会显示 ID 和实际控制器。

两种屏幕均支持黑白全刷、局刷和四级灰度，自动处理 BUSY 极性、波形、前后帧打包及休眠时序。每次刷新后进入待机，避免空闲时持续开启屏幕高压。无需手动选择型号或分别烧录固件。

硬件检查步骤及验证范围见[屏幕兼容测试说明](docs/screen_compatibility.md)。


## 项目亮点

- **桌宠不是静态吉祥物。** 它会破壳、经历五个成长阶段、形成三种性格之一，并持续记住用户的照料、对话、睡眠和外出。
- **四个统一设计的设备端应用。** 桌宠、番茄钟、状态板和答案书共享同一个启动器与视觉语言。
- **设备本身就是控制器。** 用户可以用按键、触摸、滑动、旋转和摇晃选择功能。
- **按照电子纸特性设计。** 稳定页面重视清晰度，倒计时和动画使用受控局刷，刷新过程中仍继续接收输入。
- **围绕真实硬件实现。** 固件管理显示屏与 SD 卡共享 SPI、GT911 触摸、LSM6DS3TR-C IMU、PCF8563 RTC、BQ27220 电量计、蜂鸣器、侧边按键、电池供电和深度睡眠。

## 四个应用

| 成长型桌宠 | Sticky Pomodoro Timer |
| --- | --- |
| <img src="docs/images/desktop-pet/home.png" alt="显示成长、亲密、饱腹和能量的兔子主页" width="330"> | <img src="docs/images/pomodoro/setup.png" alt="番茄钟时间选择页面" width="330"> |
| 孵化、命名、喂食、抚摸、聊天和陪玩都会影响兔子的成长、性格和对白。 | 支持 15、25、60 分钟与自定义时间，包含暂停、继续、结束确认和柔和循环提示音。 |

| Sticky Status Board | Book of Answers |
| --- | --- |
| <img src="docs/images/status-board/menu.png" alt="横屏状态板菜单" width="390"> | <img src="docs/images/book-of-answers/home.png" alt="答案书主页" width="245"> |
| 横屏展示 `BUSY`、`MEETING`、`ON CALL`、`OPEN TO TALK`、`REST` 或设备端输入的自定义状态。 | 心中默念问题并持续摇晃三秒，获得一句话答案，或 `YES`、`NO`、`UNCLEAR`。 |

## 桌宠成长系统

桌宠是固件的默认主页，也是整个项目的情感核心。真实时间由 RTC 推进，存档使用两个带校验值的 NVS 槽位轮换保存；最新存档损坏时，可以回退到另一个有效槽位。

<p align="center">
  <img src="docs/images/desktop-pet/growth-lineage.png" alt="从宠物蛋到三种成年性格的成长路线" width="640">
</p>

成长阶段包括：

1. `EGG`：连续三次明确点击完成孵化。
2. `HATCHLING`：建立最初的照料关系。
3. `CHILD`：喂食、抚摸和玩耍开始积累不同的性格倾向。
4. `YOUTH`：形成 `FOODIE`、`AFFECTIONATE` 或 `ACTIVE` 性格。
5. `ADULT`：三种路线拥有不同外形、互动动作、台词和外出纪念品。

成长值、亲密值、饱腹值、能量、心情、连续照料天数、最近动作、最近台词、性格证据和计划外出时间都会跨重启保存。平衡规则与开源方案审计记录位于 [桌宠成长系统设计](docs/desktop_pet_growth_system.md)。

| 数值 | 实际作用 |
| --- | --- |
| `GROWTH` | 通过每天的有效照料积累长期成长进度 |
| `LOVE` | 亲密等级，会改变兔子的对白和反应 |
| `FULLNESS` | 饱腹状态，通过喂食恢复 |
| `ENERGY` | 活动能力，睡眠时每分钟恢复 6% |

照料奖励采用稳定节奏：每天最多获得 10 点成长和 8 点亲密；连续照料达到 3、7、30 和 100 天时，每个里程碑只庆祝一次。兔子不是每天都会外出；生成外出计划时，它会离开 1～7 小时，用户也可以在外出页面提前叫它回家。

## 应用选择与设备交互

<p align="center">
  <img src="docs/images/launcher/launcher-landscape.png" alt="横屏应用选择器" width="700">
</p>

- 单击 AI 键或从屏幕底部上滑：打开应用选择器。
- 点击 APP 卡片：直接进入对应应用。
- 从屏幕上半部分下滑：关闭应用选择器。
- 在任意页面双击 AI 键：返回桌宠。
- 应用选择器打开时，从横屏旋转到竖屏：进入番茄钟。
- 应用选择器打开时，从竖屏旋转到横屏：进入状态板。
- 应用选择器打开时持续摇晃：进入答案书。
- 同时按住两个非 AI 侧键：进入深度睡眠。

旋转路由会等待设备停止移动并确认最终稳定方向：连续 5 个稳定样本才接受旋转；应用选择器内持续有效摇晃 800 毫秒会选择答案书，进入答案书后完整求答案过程仍要求 3 秒有效摇晃。

## 电子纸、低功耗与 RTC

电子纸断电后仍会保留图像，因此固件不会把它当成普通液晶屏反复刷新。静态页面负责建立干净画面，倒计时数字和动作区域采用范围受控的局部刷新，并通过周期性高质量刷新恢复对比度。

桌宠在进入睡眠前会安排下一项有意义的自主事件。PCF8563 RTC 可以在外出或其他计划事件前唤醒设备，而不是每隔固定时间盲目唤醒。电池百分比来自 BQ27220 电量计，充电、电量和睡眠标志会根据页面留白自动选择位置。

同时按住两个非 AI 侧键也可以主动进入深度睡眠。休眠前，当前 APP 会保存稳定页面，输入外设按顺序停止，显示屏按需要清理残影，RTC 则记录下一次有意义的唤醒时间。

## 完整视觉导览

下面展示的画面，要么直接由固件真实的 `Canvas`、字体和 1 位素材渲染生成，要么明确标注为设计过程素材。正式页面与实际设备编译使用同一套布局和素材。

### 桌宠：从宠物蛋成长为独一无二的伙伴

| 宠物蛋与主页 | 儿童期与青年期 |
| --- | --- |
| <img src="docs/images/desktop-pet/egg.png" alt="宠物蛋页面" width="235"> <img src="docs/images/desktop-pet/home-firmware-render.png" alt="固件实际渲染的桌宠主页" width="235"> | <img src="docs/images/desktop-pet/child.png" alt="儿童期兔子" width="235"> <img src="docs/images/desktop-pet/youth.png" alt="青年期兔子" width="235"> |

| 性格选择与成年期 | 睡眠与外出 |
| --- | --- |
| <img src="docs/images/desktop-pet/personality-choice.png" alt="性格选择页面" width="235"> <img src="docs/images/desktop-pet/adult.png" alt="成年期兔子" width="235"> | <img src="docs/images/desktop-pet/sleep.png" alt="兔子睡眠页面" width="235"> <img src="docs/images/desktop-pet/outing.png" alt="兔子外出页面" width="235"> |

桌宠规则与显示代码彼此独立：RTC 推进需求与年龄，用户交互更新带版本的状态对象，两个带校验值的 NVS 槽位保护存档，页面再根据成长阶段和性格选择专属兔子姿势。对白会按阶段、亲密、当前活动和最近历史筛选；存在其他候选时，优先避开刚说过的句子。

### 番茄钟：设置、专注与结束

<p align="center">
  <img src="docs/images/pomodoro/setup.png" alt="番茄钟设置页面" width="145">
  <img src="docs/images/pomodoro/custom-time.png" alt="自定义时间键盘" width="145">
  <img src="docs/images/pomodoro/running.png" alt="倒计时页面" width="145">
  <img src="docs/images/pomodoro/end-dialog.png" alt="结束确认弹窗" width="145">
  <img src="docs/images/pomodoro/alarm.png" alt="时间到页面" width="145">
</p>

主页只保留 15、25 和 60 分钟三个实用预设。自定义时间提供独立的时、分、秒字段：`CLEAR` 清空当前字段，`DELETE` 按退格规则删除。倒计时按照真实经过时间计算，刷新电子纸时仍继续接收触摸；支持暂停、原页面结束确认，以及需要用户点击 `END` 才停止的柔和循环提示音。

### 状态板：一眼看懂当前状态

| 状态菜单 | 全屏状态 |
| --- | --- |
| <img src="docs/images/status-board/menu.png" alt="状态板菜单" width="380"> | <img src="docs/images/status-board/status.png" alt="全屏状态页面" width="380"> |

| 欢迎交流 | 设备端自定义文字 |
| --- | --- |
| <img src="docs/images/status-board/open-to-talk.png" alt="OPEN TO TALK 状态" width="380"> | <img src="docs/images/status-board/custom.png" alt="自定义状态键盘" width="380"> |

`BUSY`、`MEETING`、`ON CALL`、`OPEN TO TALK`、`REST` 和 `CUSTOM` 都会打开横屏全尺寸二级页面。预设状态拥有对应的兔子场景；自定义状态提供跟手的字母/数字键盘，并在当前会话中保留输入内容。

### 答案书：提问、摇晃、思考、揭晓

<p align="center">
  <img src="docs/images/book-of-answers/home.png" alt="答案书主页" width="145">
  <img src="docs/images/book-of-answers/thinking.png" alt="思考动画" width="145">
  <img src="docs/images/book-of-answers/shake-longer.png" alt="继续摇晃提示" width="145">
  <img src="docs/images/book-of-answers/message-result.png" alt="一句话答案" width="145">
  <img src="docs/images/book-of-answers/crystal-result.png" alt="水晶球答案" width="145">
</p>

默认选择 `MESSAGE`，内置 350 条答案；`YES OR NO` 使用水晶球返回 `YES`、`NO` 或 `UNCLEAR`。主页保持安静，让三秒使用指示更容易阅读。摇晃时间不足会显示准确的重试提示；达标后会正常播放思考与揭晓动画，再显示结果。

### 横竖方向都能使用的应用选择器

| 竖屏布局 | 横屏布局 |
| --- | --- |
| <img src="docs/images/launcher/launcher-portrait.png" alt="竖屏应用选择器" width="245"> | <img src="docs/images/launcher/launcher-landscape.png" alt="横屏应用选择器" width="500"> |

AI 键发生实体按下时，IMU 会立即启动，不等待电子纸画完应用选择器。触摸选择与旋转、摇晃选择始终同时存在。目标 APP 会收到设备最终的放置方向，绘图和触摸坐标因此保持同一个正向画面。

### 六页首次开机教程

新 NVS 状态只会自动显示一次教程；之后可以从桌宠主页的小书图标重新打开。

| 欢迎页 | 照料数值 | 成长结果 |
| --- | --- | --- |
| <img src="docs/images/onboarding/tutorial-page-1.png" alt="教程欢迎页" width="220"> | <img src="docs/images/onboarding/tutorial-page-2.png" alt="桌宠数值教程" width="220"> | <img src="docs/images/onboarding/tutorial-page-3.png" alt="成长结果教程" width="220"> |

| 应用介绍 | 启动器操作 | 旋转与摇晃 |
| --- | --- | --- |
| <img src="docs/images/onboarding/tutorial-page-4.png" alt="应用介绍教程" width="220"> | <img src="docs/images/onboarding/tutorial-page-5.png" alt="应用选择器教程" width="220"> | <img src="docs/images/onboarding/tutorial-page-6.png" alt="旋转和摇晃教程" width="220"> |

## 快速开始

### 准备

- reTerminal Sticky
- 支持数据传输的 USB-C 线
- PlatformIO Core 6.1 或 PlatformIO IDE
- Python 3
- macOS、Linux 或 Windows

工程固定使用 `espressif32@6.11.0` 和 ESP-IDF 5.4.1。

### 获取并编译

```bash
git clone https://github.com/limengdu/reTerminal_Sticky_Bunny.git
cd reTerminal_Sticky_Bunny
pio run
```

默认环境为 `sticky-release`，编译结果位于：

```text
.pio/build/sticky-release/firmware.bin
```

### 烧录正式版

```bash
pio run -e sticky-release -t upload
```

需要详细日志时使用：

```bash
pio run -e sticky-debug -t upload
pio device monitor -e sticky-debug
```

串口波特率为 `115200`。成功启动时会看到 `sticky_boot: phase=ready result=ok`，随后记录当前 APP。需要清空旧桌宠存档并重新播放首次教程时，先完整擦除再上传：

```bash
pio run -e sticky-release -t erase
pio run -e sticky-release -t upload
```

## 构建环境

| 环境 | 用途 | 运行规则 | 日志 |
| --- | --- | --- | --- |
| `sticky-release` | 日常使用与发布 | 正式时间、成长上限和外出计划 | 必要警告与生命周期信息 |
| `sticky-debug` | 硬件和交互排查 | 保持正式玩法规则 | 详细应用、输入和存档日志 |
| `sticky-power-test` | 加速验证睡眠和唤醒 | 仅缩短功耗测试时间 | 功耗专项诊断 |

发布前同时构建三种环境：

```bash
pio run -e sticky-release -e sticky-debug -e sticky-power-test
```

## 工程结构

```text
src/
├── app/                 # 应用管理器、选择器、路由和生命周期
├── apps/                # 桌宠、番茄钟、状态板、答案书和教程
├── board/               # 供电、充电、共享总线与引脚配置
├── devices/             # 电池、RTC 与蜂鸣器驱动
├── display/             # 电子纸所有权与刷新操作
├── input/               # 实体按键与 GT911 触摸队列
├── sensors/             # IMU 姿态与持续摇晃会话
└── ui/                  # 画布、字体、电量叠层和像素素材

assets/                  # 原始素材、固件图片与视觉验收图
docs/images/             # README 使用的正式截图与设计演进素材
docs/desktop_pet_growth_system.md
                         # 桌宠规则与开源方案审计
test/                    # 状态、策略和页面渲染测试
tools/                   # 可重复运行的素材与数据库生成工具
third_party/             # 上游开源方案的许可证与使用说明
```

### 固件执行顺序

1. `app_main()` 首先保持电池供电锁存并初始化板级电源路径。
2. 共享 SPI 和 I²C 管理器先启动，随后初始化显示、触摸、RTC、电池、蜂鸣器、按键和 IMU。
3. 新设备进入六页教程；已经完成教程的设备读取桌宠存档并打开对应根页面。
4. APP 协调器在同一时间只把输入和显示所有权交给一个 APP。
5. 应用选择器暂停当前 APP，接收触摸与 IMU 选择，然后恢复原 APP 或切换所有权。
6. 深度睡眠前保存稳定状态、停止外设、整理显示画面并设置 RTC 唤醒时间。

公开硬件接口和 APP 接口已经补充简洁的中英双语注释。状态、策略、路由、存档记录和页面渲染尽量保持为不依赖 ESP-IDF 的纯 C++ 模块，方便直接在电脑上验证。

## 测试

无需设备即可在电脑上验证状态机和页面渲染，包括：

- 桌宠成长、离线时间、对白、动作调度和双槽存档；
- 番茄钟输入、倒计时策略、页面布局和响铃状态；
- 状态选择、自定义文字和每种状态的兔子动画；
- 答案选择、三秒持续摇晃门槛和结果页面；
- 应用选择器触摸区、滑动手势、旋转路由和功耗策略；
- 首次教程导航和六张最终页面。

```bash
./tools/run_host_tests.sh
python3 tools/check_markdown_links.py
```

页面渲染测试会在 `/tmp` 输出桌宠、番茄钟、状态板、答案书、应用选择器和六页教程的 PPM 预览。这些预览直接使用固件真实的画布、字体、触摸映射和像素素材。

### 重新生成固件素材

```bash
python3 -m pip install -r requirements-dev.txt
python3 tools/generate_desktop_pet_assets.py
python3 tools/generate_pomodoro_assets.py
python3 tools/generate_app_launcher_assets.py
python3 tools/generate_book_of_answers_assets.py
python3 tools/generate_onboarding_assets.py
python3 tools/generate_pixel_bunnies.py
```

生成后的 C++ 素材位于 `src/ui/assets/`。输入图片没有改变时，重复生成不会产生源码差异。

## 硬件与驱动对应关系

<p align="center">
  <img src="docs/images/hardware/sticky-button-layout.png" alt="reTerminal Sticky 按键与 SD 卡位置" width="620">
</p>

| 硬件路径 | 固件管理模块 |
| --- | --- |
| 电子纸与 microSD 共享 SPI | `src/board/board_shared_spi.*` 统一协调显示和 SD 使用权 |
| 地址 `0x14` 的 GT911 | `src/input/sticky_touch.*` 记录抬手点击与完整滑动轨迹 |
| 地址 `0x6A` 的 LSM6DS3TR-C | `src/sensors/sticky_imu.*` 输出即时运动、稳定方向和摇晃会话 |
| 地址 `0x51` 的 PCF8563 | `src/devices/sticky_rtc.*` 提供日期、经过时间和闹钟唤醒 |
| 地址 `0x55` 的 BQ27220 | `src/devices/sticky_battery.*` 提供真实剩余电量 |
| GPIO 48 蜂鸣器 | `src/devices/sticky_buzzer.*` 非阻塞播放 APP 和桌宠音型 |

## 常见问题

| 现象 | 检查方式 |
| --- | --- |
| 上传时无法连接 | 使用支持数据的线，关闭串口监视器，选择当前 `/dev/cu.*` 或 COM 端口，再重新上传。 |
| 上传后仍保留旧桌宠数值 | 执行上面的擦除与上传命令；普通上传按设计保留 NVS。 |
| 屏幕正常但触摸无效 | 烧录 `sticky-debug`，确认 `GT911` 日志包含 ID `911`、地址 `0x14`、传感器 `480x800` 和 `touch=polling_ready`。 |
| 出现历史画面残影 | 确认开机先全屏清白，并且周期性清理刷新仍然执行。 |
| 旋转进入了错误页面 | 把日志中的稳定 `from`、`to` 方向与设备最终真实放置方向对应检查。 |
| 短促摇晃就直接出答案 | 确认新的静止门控已经重新开启，有效峰值完整覆盖三秒提问窗口。 |
| 拔掉 USB 后设备停止工作 | 确认显示初始化之前已经记录电源锁存和充电路径日志。 |

## 设计演进

项目经历了多轮真机测试和电子纸 UI 调整。上面的代码渲染页面是正式效果；下面保留的概念素材用于说明兔子、应用选择器、首次教程和专注界面的视觉语言如何逐步形成。

<details>
<summary><strong>展开设计与素材画廊</strong></summary>

### 桌宠主页方向

<p align="center">
  <img src="docs/images/design-history/desktop-pet-home-concept.png" alt="桌宠主页概念图" width="420">
  <img src="docs/images/design-history/pet-home-reference-comparison.png" alt="桌宠主页参考与固件效果对比" width="420">
</p>

### 应用选择器方向

<p align="center">
  <img src="docs/images/design-history/app-launcher-concept.png" alt="应用选择器视觉概念" width="650">
</p>

### 首次开机教程方向

<p align="center">
  <img src="docs/images/design-history/onboarding-concept.png" alt="首次开机教程概念" width="760">
</p>

### 番茄钟引导方向

<p align="center">
  <img src="docs/images/design-history/pomodoro-guide-concept.png" alt="番茄钟引导概念" width="520">
</p>

</details>

## 参与贡献

欢迎提交 Issue 和 Pull Request。请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)，硬件行为应能追溯到 Sticky 参考实现，行为改动应增加电脑端回归测试，需要真机才能验证的改动应附带明确测试步骤。

## 许可与致谢

Sticky Bunny 使用 [MIT License](LICENSE) 发布。

桌宠核心借鉴了 TamaPoke、esp32-artoria-tamagotchi、openclaw-tamagotchi 和 ESP32-TamaPetchi 中少量 MIT 许可思路。完整许可证与审查提交记录位于 [third_party/virtual_pet/NOTICE.md](third_party/virtual_pet/NOTICE.md)。仓库中的兔子美术为本项目原创。

答案书信息数据库来自 Apache License 2.0 授权的 `DBinK/The-Book-of-Answers-Interpreter`，完整许可文本与数据库源文件一起保存。

reTerminal Sticky 是 Seeed Studio 的产品。本仓库是社区固件项目，不代表设备出厂固件。
