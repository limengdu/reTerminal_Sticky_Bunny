# reTerminal Sticky Bunny

<p align="center">
  <strong>A living virtual pet and an interactive ePaper app collection for reTerminal Sticky.</strong>
</p>

<p align="center">
  <a href="README_CN.md">简体中文</a> ·
  <a href="README_JA.md">日本語</a> ·
  <a href="https://www.seeedstudio.com/sticky/">Sticky official site</a> ·
  <a href="https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html">Product page</a> ·
  <a href="#quick-start">Get started</a> ·
  <a href="#complete-visual-tour">Visual tour</a> ·
  <a href="CHANGELOG.md">Changelog</a>
</p>

<p align="center">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/MCU-ESP32--S3-000000?style=flat-square">
  <img alt="ESP-IDF 5.4.1" src="https://img.shields.io/badge/ESP--IDF-5.4.1-000000?style=flat-square">
  <img alt="PlatformIO" src="https://img.shields.io/badge/build-PlatformIO-000000?style=flat-square">
  <img alt="Firmware 0.1.3" src="https://img.shields.io/badge/firmware-0.1.3-000000?style=flat-square">
  <img alt="MIT License" src="https://img.shields.io/badge/license-MIT-000000?style=flat-square">
  <a href="https://github.com/limengdu/reTerminal_Sticky_Bunny/actions/workflows/build.yml"><img alt="Build and test" src="https://github.com/limengdu/reTerminal_Sticky_Bunny/actions/workflows/build.yml/badge.svg"></a>
</p>

<p align="center">
  <img src="docs/images/desktop-pet/home.png" alt="Sticky Bunny virtual pet home" width="245">
  &nbsp;&nbsp;
  <img src="docs/images/launcher/launcher-portrait.png" alt="Sticky Bunny portrait app launcher" width="245">
</p>

reTerminal Sticky Bunny turns the Seeed Studio **reTerminal Sticky** into a small, persistent world: raise a rabbit that remembers your care, start a Pomodoro session, show your availability, or ask the Book of Answers. Touch, buttons, swipe gestures, device rotation, continuous shaking, RTC scheduling, and low-power ePaper behavior are designed as one coherent firmware experience.

Learn more about the device on the [Sticky official website](https://www.seeedstudio.com/sticky/) and the [reTerminal Sticky product page](https://www.seeedstudio.com/reTerminal-Sticky-p-6861.html).

This repository contains the complete PlatformIO/ESP-IDF firmware, original monochrome artwork, host-side behavior and rendering tests, release packaging, and developer documentation.

## Screen revision compatibility

Firmware **0.1.3** uses one image for Sticky displays with **SSD1677 or SSD2677**. At boot it reads register `0x70`: ID `0x07` selects SSD2677; other successfully read values select the legacy SSD1677 driver. An SPI read error aborts initialization. The boot log reports the ID and selected controller.

Both paths support full monochrome, partial updates and four-level grayscale. Controller-specific BUSY polarity, waveform selection, previous/current frame packing and sleep sequences are selected automatically. The display enters standby after each refresh so the analog supply is not left enabled while Bunny is idle. No menu choice or separate firmware is needed.

See [the compatibility test plan](docs/screen_compatibility.md) for hardware checks and verification limits.


## Why this project is different

- **A pet with continuity, not a static mascot.** It hatches, grows through five life stages, develops one of three personalities, remembers care, sleeps, speaks, plays, and occasionally goes outside.
- **Four polished on-device experiences.** The desktop pet, Pomodoro timer, status board, and Book of Answers share one launcher and one visual language.
- **The device itself is the controller.** Open the launcher by button or swipe, select by touch, rotate into portrait or landscape apps, and shake to enter the Book of Answers.
- **Designed for ePaper.** Stable screens use full or quality refreshes; time-sensitive views use bounded partial updates; input remains responsive while a refresh is in flight.
- **Built around real hardware behavior.** The firmware manages the shared display/SD SPI bus, GT911 touch controller with the verified Sticky `480 x 800` sensor mapping and reference reset recovery, LSM6DS3TR-C IMU, PCF8563 RTC, BQ27220 fuel gauge, buzzer, side buttons, battery operation, and deep sleep.

## Application gallery

| Virtual pet | Pomodoro timer |
| --- | --- |
| <img src="docs/images/desktop-pet/home.png" alt="Rabbit pet home showing growth, love, fullness and energy" width="330"> | <img src="docs/images/pomodoro/setup.png" alt="Sticky Pomodoro Timer setup screen" width="330"> |
| Hatch, name, feed, pet, talk to and play with a rabbit whose stage, personality and dialogue change over time. | Pick 15, 25 or 60 minutes, enter a custom duration, pause or end a session, and receive a gentle repeating alarm. |

| Status board | Book of Answers |
| --- | --- |
| <img src="docs/images/status-board/menu.png" alt="Sticky Status Board menu" width="390"> | <img src="docs/images/book-of-answers/home.png" alt="Book of Answers home screen" width="245"> |
| Display `BUSY`, `MEETING`, `ON CALL`, `OPEN TO TALK`, `REST`, or a custom on-device message in landscape. | Hold a question in mind and shake for three seconds to reveal a message, `YES`, `NO`, or `UNCLEAR`. |

## The virtual pet

The pet is the home screen and the emotional center of the firmware. Its state is advanced by real RTC time and stored in two checksummed NVS slots so a damaged save can fall back to the previous valid record.

<p align="center">
  <img src="docs/images/desktop-pet/growth-lineage.png" alt="Rabbit growth lineage from egg to adult personalities" width="640">
</p>

### Life and personality

1. **Egg** — three deliberate taps hatch the pet.
2. **Hatchling** — care establishes the first bond.
3. **Child** — feeding, affection and play begin shaping personality evidence.
4. **Youth** — the rabbit becomes `FOODIE`, `AFFECTIONATE`, or `ACTIVE`.
5. **Adult** — each branch receives its own proportions, actions, dialogue and keepsake.

Growth, love, fullness, energy, daily care streaks, mood, recent actions, recent dialogue, personality evidence and scheduled outings persist across restarts. The balancing model and audited upstream inspirations are documented in [the pet growth design](docs/desktop_pet_growth_system.md).

| Value | What it means |
| --- | --- |
| `GROWTH` | Long-term progress earned through valid daily care |
| `LOVE` | Bond level that changes dialogue and reactions |
| `FULLNESS` | Hunger state restored by feeding |
| `ENERGY` | Activity capacity restored at 6% per sleeping minute |

Care is deliberately paced. Each day can award up to 10 growth and 8 love; milestone celebrations happen once at 3, 7, 30, and 100 consecutive care days. The rabbit can leave home on some days, remains away for a scheduled 1–7 hours, and can be called back from the outing page.

## Launcher and physical interaction

<p align="center">
  <img src="docs/images/launcher/launcher-landscape.png" alt="Landscape launcher" width="700">
</p>

- Tap the **AI key** or swipe up from the lower screen to open the launcher.
- Tap an app card to launch it.
- Swipe down from the upper half to close the launcher.
- Double-tap the AI key from any page to return to the pet.
- Rotate from landscape to portrait while the launcher is open to enter Pomodoro.
- Rotate from portrait to landscape while the launcher is open to enter the Status Board.
- Shake while the launcher is open to enter the Book of Answers.
- Hold both non-AI side keys to enter deep sleep.

Orientation is accepted only after motion settles and the final placement is stable. Five stable samples qualify a rotation; a continuous 800 ms launcher shake selects the Book of Answers, whose own question flow requires three seconds of effective shaking.

## ePaper, power and time

The display keeps its last image without power, so the firmware treats every refresh as a limited resource. Static pages receive a clean baseline; countdown digits and animation regions use smaller refresh policies; periodic full refreshes restore contrast.

The pet schedules its next meaningful event before sleep. The PCF8563 RTC can wake the ESP32-S3 shortly before an outing or another autonomous event instead of waking at a fixed interval. Battery percentage comes from the BQ27220 fuel gauge, and the UI keeps charging and sleep indicators visible without covering app content.

Deep sleep can also be requested by holding the two non-AI side keys. Before sleeping, the active app saves a stable page, input peripherals stop safely, the display is cleaned when required, and the RTC receives the next meaningful wake time.

## Complete visual tour

Every screen below is either rendered from the firmware's real `Canvas` and one-bit assets or retained as clearly labeled design history. These are the same layouts and assets used by the device build.

### Desktop pet: from egg to an individual companion

| Egg and home | Child and youth |
| --- | --- |
| <img src="docs/images/desktop-pet/egg.png" alt="Pet egg page" width="235"> <img src="docs/images/desktop-pet/home-firmware-render.png" alt="Firmware-rendered pet home" width="235"> | <img src="docs/images/desktop-pet/child.png" alt="Child rabbit" width="235"> <img src="docs/images/desktop-pet/youth.png" alt="Youth rabbit" width="235"> |

| Personality and adulthood | Sleep and outings |
| --- | --- |
| <img src="docs/images/desktop-pet/personality-choice.png" alt="Personality choice page" width="235"> <img src="docs/images/desktop-pet/adult.png" alt="Adult rabbit" width="235"> | <img src="docs/images/desktop-pet/sleep.png" alt="Pet sleeping page" width="235"> <img src="docs/images/desktop-pet/outing.png" alt="Pet outing page" width="235"> |

The pet model is independent from the display code. RTC time advances needs and age, interactions update a versioned state object, two checksummed NVS slots protect saves, and the UI chooses a stage- and personality-specific pose. Dialogue selection filters by stage, bond, activity, and recent history so another valid sentence is preferred over an immediate repeat.

### Pomodoro: setup, focus, and finish

<p align="center">
  <img src="docs/images/pomodoro/setup.png" alt="Pomodoro setup" width="145">
  <img src="docs/images/pomodoro/custom-time.png" alt="Custom time keypad" width="145">
  <img src="docs/images/pomodoro/running.png" alt="Running countdown" width="145">
  <img src="docs/images/pomodoro/end-dialog.png" alt="End confirmation" width="145">
  <img src="docs/images/pomodoro/alarm.png" alt="Time-up alarm" width="145">
</p>

The home page keeps only the three practical presets: 15, 25, and 60 minutes. Custom input supports separate hour, minute, and second fields; `CLEAR` resets the active field, while `DELETE` behaves as backspace. The countdown uses real elapsed time, keeps touch responsive during refresh, supports pause and an in-place end confirmation, and repeats a gentle alarm until `END` is tapped.

### Status Board: one glance, one clear state

| Menu | Full status |
| --- | --- |
| <img src="docs/images/status-board/menu.png" alt="Status Board menu" width="380"> | <img src="docs/images/status-board/status.png" alt="Full-screen status" width="380"> |

| Open to talk | On-device custom text |
| --- | --- |
| <img src="docs/images/status-board/open-to-talk.png" alt="Open to talk status" width="380"> | <img src="docs/images/status-board/custom.png" alt="Custom status keyboard" width="380"> |

`BUSY`, `MEETING`, `ON CALL`, `OPEN TO TALK`, `REST`, and `CUSTOM` each open a full landscape second-level page. Preset pages use a matching rabbit scene; the custom page provides a responsive letters/numbers keyboard and stores the selected text for the current session.

### Book of Answers: ask, shake, think, reveal

<p align="center">
  <img src="docs/images/book-of-answers/home.png" alt="Book of Answers home" width="145">
  <img src="docs/images/book-of-answers/thinking.png" alt="Thinking animation" width="145">
  <img src="docs/images/book-of-answers/shake-longer.png" alt="Shake longer instruction" width="145">
  <img src="docs/images/book-of-answers/message-result.png" alt="Message answer" width="145">
  <img src="docs/images/book-of-answers/crystal-result.png" alt="Crystal answer" width="145">
</p>

`MESSAGE` is selected by default and draws from 350 embedded answers. `YES OR NO` uses the crystal ball and returns `YES`, `NO`, or `UNCLEAR`. The home page stays visually quiet so the three-second instruction remains easy to read. A short shake opens an exact retry message; a qualified shake continues through thinking and reveal animations before showing the result.

### Launcher in both physical orientations

| Portrait layout | Landscape layout |
| --- | --- |
| <img src="docs/images/launcher/launcher-portrait.png" alt="Portrait app launcher" width="245"> | <img src="docs/images/launcher/launcher-landscape.png" alt="Landscape app launcher" width="500"> |

The launcher starts IMU monitoring as soon as the AI key is physically pressed, before the ePaper refresh begins. Touch selection and motion selection remain available together. The selected app receives the device's final placement so both drawing and touch coordinates use the same visible direction.

### Six-page first-boot guide

The guide appears once for a new NVS state and can be reopened from the illustrated book on the pet home screen.

| Welcome | Care values | Growth results |
| --- | --- | --- |
| <img src="docs/images/onboarding/tutorial-page-1.png" alt="Tutorial welcome page" width="220"> | <img src="docs/images/onboarding/tutorial-page-2.png" alt="Pet values tutorial" width="220"> | <img src="docs/images/onboarding/tutorial-page-3.png" alt="Pet growth outcomes tutorial" width="220"> |

| Applications | Launcher controls | Rotation and shake |
| --- | --- | --- |
| <img src="docs/images/onboarding/tutorial-page-4.png" alt="Applications tutorial" width="220"> | <img src="docs/images/onboarding/tutorial-page-5.png" alt="Launcher controls tutorial" width="220"> | <img src="docs/images/onboarding/tutorial-page-6.png" alt="Rotation and shake tutorial" width="220"> |

## Hardware target

| Component | Firmware use |
| --- | --- |
| ESP32-S3R8 | Application, graphics, input routing and low-power control |
| 800 × 480 monochrome ePaper | Portrait and landscape application UI |
| GT911 | Capacitive touch and swipe input |
| LSM6DS3TR-C | Stable orientation, motion sessions and continuous shake detection |
| PCF8563 | Calendar time and scheduled wake-up |
| BQ27220 | Battery state of charge |
| Buzzer | Context-aware pet sounds and Pomodoro alarm |
| MicroSD slot | Shared SPI hardware path prepared safely at boot |

The implementation follows the board initialization and driver behavior demonstrated by Seeed Studio's Sticky hardware examples.

## Quick start

### Requirements

- reTerminal Sticky
- USB-C data cable
- PlatformIO Core 6.1 or PlatformIO IDE
- Python 3
- macOS, Linux, or Windows

The project pins `espressif32@6.11.0` and builds against ESP-IDF 5.4.1.

### Clone and build

```bash
git clone https://github.com/limengdu/reTerminal_Sticky_Bunny.git
cd reTerminal_Sticky_Bunny
pio run
```

`sticky-release` is the default environment. A successful build creates:

```text
.pio/build/sticky-release/firmware.bin
```

### Upload the release firmware

```bash
pio run -e sticky-release -t upload
```

For development logs:

```bash
pio run -e sticky-debug -t upload
pio device monitor -e sticky-debug
```

The monitor runs at `115200` baud. A successful boot reaches `sticky_boot: phase=ready result=ok` and then reports the active app. To start from a completely new pet and replay onboarding, erase the flash and upload again:

```bash
pio run -e sticky-release -t erase
pio run -e sticky-release -t upload
```

## Build profiles

| Environment | Purpose | Runtime rules | Logging |
| --- | --- | --- | --- |
| `sticky-release` | Daily use and releases | Production time, limits and outing schedule | Essential warnings and lifecycle events |
| `sticky-debug` | Hardware and interaction diagnosis | Production gameplay rules | Detailed app, input and storage logs |
| `sticky-power-test` | Accelerated sleep/wake validation | Shortened power timing only | Power-focused diagnostics |

Build every profile before a release:

```bash
pio run -e sticky-release -e sticky-debug -e sticky-power-test
```

## Repository map

```text
src/
├── app/                 # App manager, launcher, routing and lifecycle
├── apps/                # Pet, Pomodoro, Status Board, Answers and onboarding
├── board/               # Power, charger, shared buses and pin configuration
├── devices/             # Battery, RTC and buzzer drivers
├── display/             # ePaper ownership and refresh operations
├── input/               # Buttons and GT911 touch queue
├── sensors/             # IMU orientation and shake sessions
└── ui/                  # Canvas, font, overlays and generated pixel assets

assets/                  # Original art, firmware-ready images and QA renders
docs/images/             # Curated README screenshots and design history
docs/desktop_pet_growth_system.md
                         # Detailed pet rules and source audit
test/                    # Native state, policy and rendering tests
tools/                   # Deterministic asset and database generators
third_party/             # License notices for adapted open-source ideas
```

### Firmware execution flow

1. `app_main()` holds the battery-power latch and initializes the board power paths.
2. Shared SPI and I²C owners start before display, touch, RTC, battery, buzzer, buttons, and IMU clients.
3. New devices enter the six-page tutorial; completed devices restore the pet save and open the correct root page.
4. The app coordinator gives exactly one app input and display ownership at a time.
5. The launcher pauses the active app, captures touch and IMU choices, then resumes or switches ownership.
6. Before deep sleep, stable state is saved, peripherals stop, the display is prepared, and the RTC alarm is programmed.

Public hardware and app interfaces use concise bilingual comments. Pure C++ state, policy, routing, storage-record, and rendering modules remain independent from ESP-IDF wherever possible so behavior can be verified on a development computer.

## Tests and visual QA

The native tests exercise state machines and render pages into PPM files without requiring the device. Coverage includes:

- pet progression, offline time, dialogue, animation scheduling and dual-slot saves;
- Pomodoro input, countdown policy, page layout and alarm states;
- status selection, custom text and status-specific animation;
- answer selection, three-second shake qualification and result layouts;
- launcher touch zones, swipe gestures, orientation routing and power policy;
- onboarding navigation and all six final pages.

```bash
./tools/run_host_tests.sh
python3 tools/check_markdown_links.py
```

Rendering tests write PPM previews to `/tmp`, including pet, Pomodoro, Status Board, Book of Answers, launcher, and all onboarding pages. These previews use the firmware's actual canvas, font, touch maps, and generated pixel assets.

### Asset regeneration

```bash
python3 -m pip install -r requirements-dev.txt
python3 tools/generate_desktop_pet_assets.py
python3 tools/generate_pomodoro_assets.py
python3 tools/generate_app_launcher_assets.py
python3 tools/generate_book_of_answers_assets.py
python3 tools/generate_onboarding_assets.py
python3 tools/generate_pixel_bunnies.py
```

Generated C++ assets are written under `src/ui/assets/`. Re-running the generators with unchanged inputs produces no source diff.

## Hardware and driver map

<p align="center">
  <img src="docs/images/hardware/sticky-button-layout.png" alt="reTerminal Sticky button and SD card layout" width="620">
</p>

| Hardware path | Firmware owner |
| --- | --- |
| ePaper and microSD shared SPI | `src/board/board_shared_spi.*` serializes display and SD ownership |
| GT911 at `0x14` | `src/input/sticky_touch.*` records released taps and complete swipe paths |
| LSM6DS3TR-C at `0x6A` | `src/sensors/sticky_imu.*` reports observed motion, stable placement, and shake sessions |
| PCF8563 at `0x51` | `src/devices/sticky_rtc.*` supplies date, elapsed time, and alarm wake |
| BQ27220 at `0x55` | `src/devices/sticky_battery.*` supplies charge percentage |
| GPIO 48 buzzer | `src/devices/sticky_buzzer.*` plays non-blocking app and pet patterns |

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Upload cannot connect | Use a data-capable cable, close the serial monitor, select the current `/dev/cu.*` or COM port, then retry upload. |
| Old pet values remain | Run the erase and upload commands above; uploading alone preserves NVS by design. |
| Display works but touch does not | Use `sticky-debug` and confirm `GT911` reports ID `911`, address `0x14`, sensor `480x800`, and `touch=polling_ready`. |
| Screen contains an old ghost image | Confirm boot performs a white full clear and that periodic cleanup refreshes still occur. |
| Rotation selects the wrong page | Compare the settled `from` and `to` orientation log with the device's physical final placement. |
| Short shake immediately reveals an answer | Confirm a fresh quiet gate is armed and effective peaks span the complete three-second question window. |
| Battery operation stops after USB removal | Confirm the boot log reports the power latch and charger path before display initialization. |

## Design history

The project grew through physical-device testing and repeated ePaper UI studies. The current code-rendered screens above are the shipped source of truth; the selected concepts below show how the rabbit, launcher, tutorial, and focus language developed.

<details>
<summary><strong>Open the design and asset gallery</strong></summary>

### Desktop pet home direction

<p align="center">
  <img src="docs/images/design-history/desktop-pet-home-concept.png" alt="Desktop pet home concept" width="420">
  <img src="docs/images/design-history/pet-home-reference-comparison.png" alt="Pet home reference and firmware comparison" width="420">
</p>

### App launcher direction

<p align="center">
  <img src="docs/images/design-history/app-launcher-concept.png" alt="App launcher visual concept" width="650">
</p>

### First-boot tutorial direction

<p align="center">
  <img src="docs/images/design-history/onboarding-concept.png" alt="First-boot tutorial concept" width="760">
</p>

### Pomodoro guidance direction

<p align="center">
  <img src="docs/images/design-history/pomodoro-guide-concept.png" alt="Pomodoro guide concept" width="520">
</p>

</details>

## Contributing

Issues and pull requests are welcome. Start with [CONTRIBUTING.md](CONTRIBUTING.md), keep hardware behavior traceable to the Sticky reference implementation, add a native regression test for behavior changes, and include a device validation plan when hardware is required.

## Credits and licenses

Sticky Bunny is released under the [MIT License](LICENSE).

The pet architecture adapts small MIT-licensed ideas from TamaPoke, esp32-artoria-tamagotchi, openclaw-tamagotchi and ESP32-TamaPetchi. The original notices and exact audited commits are recorded in [third_party/virtual_pet/NOTICE.md](third_party/virtual_pet/NOTICE.md). Rabbit artwork in this repository is original to this project.

The Book of Answers message database is derived from `DBinK/The-Book-of-Answers-Interpreter` under the Apache License 2.0; its complete notice is stored with the source database.

reTerminal Sticky is a product of Seeed Studio. This is a community firmware project and is not presented as the device's factory firmware.
