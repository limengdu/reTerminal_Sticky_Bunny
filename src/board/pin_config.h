#pragma once

// Board power latch pins / 板级电源锁存引脚
#define PIN_POWER_HOLD 45
#define PIN_POWER_LOCK 46

// Top AI/OK button from the hardware reference / 硬件示例中的顶部AI/OK按键
#define PIN_TOP_BUTTON 4

// Side buttons used as the deliberate sleep chord / 用于组合休眠操作的两个侧键
#define PIN_SIDE_BUTTON_LEFT 5
#define PIN_SIDE_BUTTON_RIGHT 6

// BQ25616 charger and USB-power detection / BQ25616充电和USB电源检测
#define PIN_BAT_CHG_EN 39
#define PIN_EXTERNAL_POWER 9

// Shared sensor bus on I2C1 / I2C1上的共享传感器总线
#define PIN_SENSOR_SCL 0
#define PIN_SENSOR_SDA 1
#define BQ27220_I2C_ADDR 0x55
#define PCF8563_I2C_ADDR 0x51
#define LSM6DS3_I2C_ADDR 0x6A

// SSD1677/SSD2677 e-paper panel on SPI2 / SPI2总线上的双版本电子纸屏幕
#define PIN_EPD_MOSI 14
#define PIN_EPD_CLK 13
#define PIN_EPD_MISO 12
#define PIN_EPD_CS 15
#define PIN_EPD_DC 16
#define PIN_EPD_RST 17
#define PIN_EPD_BUSY 18
#define PIN_EPD_EN 47

// MicroSD shares SPI2 data and clock lines with the e-paper panel.
// MicroSD与电子纸共用SPI2的数据线和时钟线，通过独立CS引脚避免总线冲突。
#define PIN_SD_CS 8
#define PIN_SD_EN 10
#define PIN_SD_DETECT 11
#define PIN_SD_MOSI PIN_EPD_MOSI
#define PIN_SD_MISO PIN_EPD_MISO
#define PIN_SD_CLK PIN_EPD_CLK

// GT911 touch controller on I2C0 / I2C0总线上的GT911触摸控制器
#define PIN_TOUCH_SCL 2
#define PIN_TOUCH_SDA 3
#define PIN_TOUCH_EN 42
#define PIN_TOUCH_INT 21
#define PIN_TOUCH_RST 41

// Onboard passive buzzer / 板载无源蜂鸣器
#define PIN_BUZZER 48
