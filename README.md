<p align="center">
  <picture>
    <source srcset="images/logo-pulse-dark.svg" media="(prefers-color-scheme: dark)" />
    <img src="images/logo-pulse-light.svg" alt="Pulse Hardware" width="200" />
  </picture>
</p>

<h1 align="center">Pulse Hardware</h1>

<p align="center">
  <strong>Nimo 提醒助手 · ESP32-S3 硬件伴侣</strong><br/>
  固件 · 原理图 · 引脚定义 · 烧录指南
</p>

<p align="center">
  <a href="#"><img src="https://img.shields.io/badge/MCU-ESP32--S3-blue?style=flat-square" alt="MCU" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Display-ST7789-orange?style=flat-square" alt="Display" /></a>
  <a href="#"><img src="https://img.shields.io/badge/Audio-ES8311-green?style=flat-square" alt="Audio" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg?style=flat-square" alt="License" /></a>
</p>

---

## 这是什么？

Nimo 硬件伴侣是一个基于 **ESP32-S3** 的桌面智能硬件，与 [Pulse](https://github.com/mustaf-osman/pulse) AI 助手配合使用。它能：

- 📟 显示提醒文字、时间、表情（ST7789 彩色 LCD）
- 🔊 语音播报提醒（ES8311 音频编解码 + I2S 功放）
- 💡 RGB 灯光指示提醒状态
- 📡 通过 WiFi 连接 Pulse 后端，自动拉取和上报任务

## 硬件规格

| 模块 | 型号 | 说明 |
|------|------|------|
| 主控 | ESP32-S3 | 双核 Xtensa LX7, WiFi/BLE |
| 屏幕 | ST7789 1.54" TFT | 240×240, SPI 接口 |
| 音频 DAC | ES8311 | I2S 音频编解码 |
| 功放 | NS4150B | 3W D 类功放 |
| GPIO 扩展 | XL9555 | I2C 扩展 16 路 IO |
| 电源 | 7.4V 2000mAh 锂电池 | Type-C 充电 |

## 引脚定义

### SPI 显示屏 (ST7789)

| 信号 | GPIO | 说明 |
|------|------|------|
| MOSI | 47 | SPI 数据 |
| SCLK | 21 | SPI 时钟 |
| CS | 2 | 片选（直连） |
| DC | 3 | 数据/命令 |
| CS (XL9555) | P15 | 片选（经扩展器） |
| BL (XL9555) | P16 | 背光控制 |

### I2S 音频 (ES8311)

| 信号 | GPIO | 说明 |
|------|------|------|
| MCLK | 45 | 主时钟 |
| BCLK | 39 | 位时钟 |
| LRCK | 41 | 左右声道 |
| DOUT | 42 | 数据输出 |
| DIN | 40 | 数据输入 |

### I2C 总线

| 总线 | SDA | SCL |
|------|-----|-----|
| I2C0 (XL9555) | 38 | 48 |
| I2C1 (ES8311) | 4 | 5 |

### XL9555 GPIO 扩展器 (地址 0x20)

| 端口 | 功能 |
|------|------|
| P15 | LCD 片选信号 |
| P16 | 背光使能 (HIGH=亮) |
| P17 | 音频静音 (HIGH=播放) |
| P04 | 按键输入 (低有效) |

### 其他

| 信号 | GPIO | 说明 |
|------|------|------|
| BOOT/KEY | 0 | 板载按键，低有效 |

## 固件列表

| 固件 | 说明 |
|------|------|
| `nimo-esp32s3-minimal/` | **主固件** — WiFi 连接、设备注册、心跳、拉取提醒、屏幕显示、音频播放 |
| `nimo-esp32s3-speaker-test/` | 扬声器测试固件 |
| `nimo-hiwonder-screen-test/` | 海沃德屏幕初始化测试 |
| `nimo-hmi-bus-probe/` | I2C 总线设备扫描 (v1) |
| `nimo-hmi-bus-probe-v2/` | I2C 总线设备扫描 (v2) |
| `nimo-hmi-probe/` | HMI 外设探测 |
| `nimo-screen-scan/` | I2C 地址扫描 + 屏幕初始化 |

## 快速开始

### 1. 硬件接线

按照上方引脚定义连接 ESP32-S3 与各外设模块。

### 2. 安装 Arduino 环境

- 安装 [Arduino IDE](https://www.arduino.cc/en/software)
- 添加 ESP32 开发板支持（`https://espressif.github.io/arduino-esp32/package_esp32_index.json`）
- 安装依赖库：
  - Adafruit GFX Library
  - Adafruit ST7789
  - U8g2 for Adafruit GFX
  - ESP_I2S

### 3. 配置 WiFi

```bash
cp firmware/nimo-esp32s3-minimal/private_config.example.h firmware/nimo-esp32s3-minimal/private_config.h
```

编辑 `private_config.h`：

```cpp
const char* WIFI_SSID = "你的WiFi名";
const char* WIFI_PASSWORD = "你的WiFi密码";
const char* NIMO_BASE_URL = "http://10.232.178.175:3721";
```

### 4. 烧录

1. Arduino IDE → 选择开发板 `ESP32S3 Dev Module`
2. 选择端口（如 `COM11`）
3. 按住板子 `BOOT` 键 → 点击上传 → 出现 `Connecting...` 时松开
4. 打开串口监视器（115200 波特率）验证输出

### 5. 验证

串口监视器应看到：

```
WiFi connected
POST /device/register → 200
POST /device/heartbeat → 200
GET /device/next-reminder → 200
```

打开 Pulse Brain UI → 设备页面 → 刷新，应看到 `Nimo ESP32-S3` 在线。

## 通信协议

设备通过 HTTP 与 Pulse 后端通信：

| 方法 | 路径 | 说明 | 频率 |
|------|------|------|------|
| POST | /device/register | 设备注册 | 启动时 + 每 60s |
| POST | /device/heartbeat | 心跳上报 | 每 30s |
| GET | /device/next-reminder | 拉取待提醒任务 | 每 10s |
| GET | /device/speech | 拉取待播报语音 | 每 3.5s |
| POST | /device/done | 上报任务完成 | 事件触发 |
| POST | /device/snooze | 上报稍后提醒 | 事件触发 |

## 项目结构

```
nimo-hardware/
├── firmware/
│   ├── nimo-esp32s3-minimal/      # 主固件 (核心)
│   ├── nimo-esp32s3-speaker-test/ # 扬声器测试
│   ├── nimo-hiwonder-screen-test/ # 屏幕测试
│   ├── nimo-hmi-bus-probe/        # I2C 探测 v1
│   ├── nimo-hmi-bus-probe-v2/     # I2C 探测 v2
│   ├── nimo-hmi-probe/            # HMI 探测
│   └── nimo-screen-scan/          # 屏幕扫描
├── schematics/
│   └── ESP32S3开发板原理图_V1.1.pdf
├── doc/
│   ├── Nimo_提醒助手_产品与硬件方案_V1.md
│   └── 交接文档-硬件接入续接.md
└── images/
```

## 依赖的软件端

本硬件需要配合 [Pulse](https://github.com/mustaf-osman/pulse) 使用。确保 Pulse 以 LAN 模式启动：

```bash
cd pulse
npm run start:lan
```

## 故障排查

| 问题 | 解决 |
|------|------|
| 烧录失败 | 按住 BOOT 键再点上传，出现 Connecting 时松开 |
| 端口消失 | 拔插 USB，重新选择 COM 口 |
| WiFi 连不上 | 检查 `private_config.h` 中的 SSID 和密码 |
| 注册失败 | 确认 Pulse 已启动且 NIMO_BASE_URL 地址正确 |
| 屏幕不亮 | 检查 XL9555 背光和 CS 信号 |

## 许可证

MIT License — 详见 [LICENSE](./LICENSE)。

---

<p align="center">
  配合 <a href="https://github.com/mustaf-osman/pulse">Pulse</a> 使用 · Made with ❤️
</p>
