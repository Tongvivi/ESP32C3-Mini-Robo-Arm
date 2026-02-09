# ESP32-C3 Mini Robo Arm 控制模块

[English Version](README_EN.md)

这是一个基于 ESP32-C3 的小型机械臂控制固件，提供 Web 控制界面与 WebSocket 实时控制，支持多 WiFi 记忆、自动配网与心跳状态反馈。

在线串口终端（可用于查看串口日志）:
https://googlechromelabs.github.io/serial-terminal/

## 项目图片
![Robo Arm 3](img-3.PNG)
![Robo Arm 1](img-1.PNG)
![Robo Arm 2](img-2.JPEG)


## 主要功能
- Web 控制面板（LittleFS 静态页面）
- WebSocket 实时控制与心跳状态
- 多 WiFi 记忆与自动重连
- 配网模式自动开启（AP 热点）
- 360 度舵机 x2、180 度舵机 x1、TT 电机 x1 控制

## 硬件与引脚
- SERVO1_PIN: GPIO2 (360 度舵机 1)
- SERVO2_PIN: GPIO3 (360 度舵机 2)
- SERVO3_PIN: GPIO4 (180 度舵机)
- MOTOR_IN1: GPIO0 (TT 电机)
- MOTOR_IN2: GPIO1 (TT 电机)
- LED_PIN: GPIO8 (状态指示灯)

## 目录结构
- `src/main.cpp`: 主控固件
- `data/index.html`: Web 控制页面
- `data/style.css`: 页面样式
- `data/script.js`: 前端逻辑与 WebSocket 协议
- `platformio.ini`: 构建与依赖配置

## 依赖库
在 `platformio.ini` 中配置:
- WiFiManager
- ESP32Servo
- WebSockets
- ESPAsyncWebServer + AsyncTCP
- ArduinoJson

## 快速开始
1) 使用 PlatformIO 打开工程
2) 编译并烧录固件
```bash
pio run -t upload
```
3) 烧录 LittleFS 静态资源
```bash
pio run -t uploadfs
```
4) 串口波特率: `115200`

## WiFi 配网与重置
- 设备会优先连接已保存 WiFi
- 若连接失败，自动开启 AP 配网
  - SSID: `ESP32-RobotArm-<MAC>`
  - 进入后使用浏览器完成配网
- 重置 WiFi:
  - 访问 `http://<device-ip>/reset-wifi`

## Web 控制界面
浏览器访问:
```
http://<device-ip>/
```
WebSocket 地址:
```
ws://<device-ip>:81
```

## WebSocket 协议
客户端发送:
```json
{"type":"run_duration","motor":"servo1","speed":130,"duration":100}
{"type":"start_continuous","motor":"servo2","speed":130}
{"type":"stop","motor":"motor"}
{"type":"servo180","angle":90}
```
服务端心跳:
```json
{"type":"heartbeat","timestamp":12345,"rssi":-60,"uptime":12,"ssid":"MyWiFi"}
```

## HTTP API
- `GET /wifi-info` 获取 WiFi 状态与已保存 SSID 列表
- `GET /reset-wifi` 清除 WiFi 配置并重启

## 开源资源
项目已完全开源，欢迎大家体验、复刻！
这是一个基于 ESP32 的有趣 DIY 机械臂项目（L-ONE / ArmBot），制作过程清晰，性价比很高。

#1 3D 模型结构件
来自 MakerWorld 优秀作者 Leng入瑶 的设计，细节完善，打印友好。
模型链接: https://makerworld.com.cn/zh/models/1760642
重要提醒: 请选择「适配第三方舵机 + TT 马达版本」配置。
如果不想动手，可直接打印原作者模型并购买拓竹 Cyberbrick 套件。

#2 PCB 控制板
开源硬件地址（嘉立创开源硬件平台）:
https://oshwhub.com/tonytang2/esp32-armbot
嘉立创可免费打 PCB 电路板。

#3 完整材料清单 + 详细教程 + 固件
一站式攻略: https://gizfolo.com/p/robot-arm
包含 BOM、组装步骤、固件烧录、WiFi 配网、B 站视频教程等。

## 说明
本项目为开源 DIY 机械臂控制模块，欢迎交流与复刻，Enjoy!
