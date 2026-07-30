#pragma once

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 云端模式（推荐，任何网络可用）：
const char* NIMO_BASE_URL = "https://cloud.agent1.xin/nimo";
// 局域网模式（需电脑运行 npm run start:lan）：
// const char* NIMO_BASE_URL = "http://10.232.178.175:3721";

// 云端 API Key（必须用 #define；连局域网电脑时可留空 ""）
#define NIMO_API_KEY "YOUR_NIMO_API_KEY"

const char* DEVICE_ID = "nimo-esp32s3-001";
const char* DEVICE_NAME = "Nimo ESP32-S3";
