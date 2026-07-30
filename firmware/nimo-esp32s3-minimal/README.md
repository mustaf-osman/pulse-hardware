# Nimo ESP32-S3 最小接入固件

这个模板用于先跑通硬件到 Nimo 的最小链路：

1. ESP32-S3 连接 WiFi
2. 向 Nimo 注册设备
3. 定时发送心跳
4. 定时拉取下一条提醒

## 当前电脑识别状态

- 串口：`COM11`
- USB 设备：`USB 串行设备 (COM11)` / `USB JTAG/serial debug unit`
- Nimo LAN 地址：`http://10.232.178.175:3721`

## 烧录前准备

1. 保持 Nimo 使用 LAN 模式启动：

   ```powershell
   npm run start:lan
   ```

2. 打开 Arduino IDE。
3. 安装 ESP32 开发板支持。
4. 选择开发板：优先选择 `ESP32S3 Dev Module`。
5. 选择端口：`COM11`。
6. 打开 `nimo_esp32s3_minimal.ino`。
7. WiFi 和 Nimo 地址已经写在本地私有配置里：

   ```cpp
   private_config.h
   ```

8. 如果换网络，只改 `private_config.h`，不要改主固件：

   ```cpp
   const char* WIFI_SSID = "你的WiFi名";
   const char* WIFI_PASSWORD = "你的WiFi密码";
   const char* NIMO_BASE_URL = "http://10.232.178.175:3721";
   ```

## 验证方式

烧录后打开 Arduino 串口监视器：

- 波特率：`115200`
- 看到 `WiFi connected` 表示联网成功
- 看到 `POST /device/register -> 200` 表示注册成功
- 看到 `POST /device/heartbeat -> 200` 表示心跳成功
- 看到 `GET /device/next-reminder -> 200` 表示能拉取提醒

然后打开 Nimo 的「设备」页面，点击刷新，应该能看到 `Nimo ESP32-S3` 在线。

## 如果烧录失败

- 按住板子上的 `BOOT` 键，再点 Arduino IDE 上传。
- 出现 `Connecting...` 时松开 `BOOT`。
- 如果端口不见了，拔插 USB 后重新选择 `COM11` 或新的 COM 口。

## 下一步

最小链路跑通后，再做屏幕显示、按键完成提醒、蜂鸣器提醒、触摸稍后提醒。
