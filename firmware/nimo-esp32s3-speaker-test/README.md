# Nimo ESP32-S3 喇叭测试固件

这是 Nimo 接入硬件语音的第一步：**只验证板载喇叭能不能出声**。
跑通这一步后，再把它接到 Nimo 后端的 `/device/speech/*` 接口播放 TTS。

## 它做什么

1. 通过 I2C 扫描设备，确认 ES8311 在线（地址 `0x18`）。
2. 用最小寄存器序列初始化 ES8311（16kHz / 16bit / Mono / I2S Slave / MCLK 来自 MCLK 引脚）。
3. 把几个候选 PA 使能引脚拉高（防止 NS4150B 处于静音）。
4. 用 I2S 持续输出 440Hz 正弦波到 ES8311 → NS4150B → 喇叭。

## 硬件引脚（已确认）

- I2C 控制总线：`SDA = GPIO4`、`SCL = GPIO5`
- I2S 时钟：`MCLK = GPIO45`、`BCLK = GPIO39`、`LRCK = GPIO41`
- I2S 数据：`DOUT(ESP→Codec) = GPIO42`、`DIN(Codec→ESP) = GPIO40`
- 功放：`NS4150B`（`CTRL/MUTE` 引脚的 GPIO 还未在原理图中明确，所以脚本会
  把几个常见候选脚拉高一次以确保不被静音）

## 烧录步骤

1. 打开 Arduino IDE。
2. **开发板**：`ESP32S3 Dev Module`。
3. **端口**：`COM11`（如果不一样请改成你电脑识别到的口）。
4. **PSRAM**：`OPI PSRAM`。
5. **Flash Size**：`16MB`。
6. 打开 `nimo-esp32s3-speaker-test.ino`，点上传。
7. 上传完成后打开串口监视器，波特率 `115200`。

## 预期结果

串口应输出：

```
[I2C] scanning bus...
[I2C]   found device at 0x18
[I2C]   found device at ...（如果还有别的 I2C 设备会一起列出）
[ES8311] init done.
[ES8311] reg 0x00=0x80 (expect 0x80), reg 0x32=0xBF (expect 0xBF)
[PA] driving candidate enable pins HIGH (safe probe)...
[I2S] begin OK, streaming 440Hz sine @16kHz mono 16-bit
```

同时**喇叭应能持续听到一个不大不小的 440Hz 持续音**，类似拨号音。

## 如果没有声音

按这个顺序排查，并告诉我具体停在哪一步：

1. 串口找不到 `0x18`：
   - 说明 ES8311 没被识别到。一般是 I2C 引脚错了或者上拉缺失。
   - 大概率不是软件问题，需要再贴一次 ESP32-S3 那张图，确认 `SDA1/SCL1` 接的是哪两个脚。
2. `0x18` 找到了，但 `[ES8311] init done.` 后喇叭无声：
   - 大概率是 NS4150B 的 `CTRL/MUTE` 还在静音。
   - 我会根据你给的 PA 区高清原理图，再补一个对应 GPIO 拉高的逻辑。
3. 声音很小、很糊、断断续续：
   - 一般是 MCLK 分频不对。
   - 把这条信息告诉我，我换 24kHz / 48kHz 再试。
4. 上传时报 `ESP_I2S.h: No such file or directory`：
   - Arduino IDE 的 `ESP32` 开发板支持版本太低。请在「开发板管理器」升级到 `esp32` 3.0 以上。

## 接下来

喇叭能出声后，我会把这套初始化合并到主固件 `nimo-esp32s3-minimal`，
让它从 `GET /device/speech/next` 拉到任务、`GET /device/speech/audio` 拉到音频，
经过解码后写到这条 I2S 链路上播放。
