#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

// 先包含 WiFiManager 相关
#include <WiFiManager.h>
#include <WiFiMulti.h>

// 再包含 AsyncWebServer 相关
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>

// 其他头文件
#include <ESP32Servo.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Ticker.h>
#include <esp_sleep.h>

// ========== 硬件引脚定义 ==========
#define SERVO1_PIN 2    // 360度舵机1
#define SERVO2_PIN 3    // 360度舵机2
#define SERVO3_PIN 4    // 180度舵机
#define MOTOR_IN1 0     // TT电机
#define MOTOR_IN2 1
#define LED_PIN 8       // 状态指示LED

// ========== WiFi配置 ==========
#define MAX_WIFI_NETWORKS 5  // 最多记住5个WiFi
#define WIFI_CONNECT_TIMEOUT 15000  // 单个WiFi连接超时（毫秒）

// ========== 舵机对象 ==========
Servo servo1;
Servo servo2;
Servo servo3;

// ========== 服务器对象 ==========
AsyncWebServer server(80);
WebSocketsServer webSocket(81);

// ========== WiFi对象 ==========
WiFiMulti wifiMulti;
Preferences preferences;

// ========== WiFi信息结构 ==========
struct WiFiCredential {
  String ssid;
  String password;
};

// ========== 电机状态结构 ==========
struct MotorState {
  int speed;
  unsigned long startTime;
  unsigned long duration;
  bool running;
  bool autoStop;
};

MotorState servo1State = {90, 0, 0, false, false};
MotorState servo2State = {90, 0, 0, false, false};
MotorState motorState = {0, 0, 0, false, false};

// ========== 心跳相关 ==========
unsigned long lastHeartbeat = 0;

// ========== 指令超时休眠 ==========
const unsigned long COMMAND_IDLE_TIMEOUT_MS = 5UL * 60UL * 1000UL;
unsigned long lastCommandTime = 0;

// ========== WiFi信号强度 ==========
unsigned long lastWiFiCheck = 0;

// ========== LED闪烁控制 ==========
unsigned long lastLedBlink = 0;
bool ledState = false;
bool configModeActive = false;
Ticker ledTicker;

// ========== 函数声明 ==========
void setupWiFi();
void setupWebServer();
void setupWebSocket();
void handleWebSocketMessage(uint8_t num, uint8_t* payload, size_t length);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
void checkServo1AutoStop();
void checkServo2AutoStop();
void checkMotorAutoStop();
void sendHeartbeatIfNeeded();
void sendHeartbeat();
void startServo1(int speed, int duration, bool autoStop);
void stopServo1();
void startServo2(int speed, int duration, bool autoStop);
void stopServo2();
void startMotor(int speed, int duration, bool autoStop, bool forward);
void stopMotor();
void setServo3Angle(int angle);
void blinkLED(int interval);
void toggleLed();
void configModeCallback(WiFiManager *myWiFiManager);
void saveWiFiCredentials(String ssid, String password);
int loadWiFiCredentials();
bool connectToSavedWiFi();
void printSavedNetworks();
void checkInactivitySleep();
void enterDeepSleep();

// ========== Setup ==========
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println("ESP32-C3 电机控制系统启动");
  Serial.println("========================================");
  
  // 初始化LED引脚
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("✓ LED初始化完成");
  delay(1000);
  digitalWrite(LED_PIN, LOW);
  
  
  // 初始化LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("✗ LittleFS挂载失败");
    return;
  }
  Serial.println("✓ LittleFS挂载成功");
  
  // 初始化舵机
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  servo3.attach(SERVO3_PIN);
  
  servo1.write(90);  // 停止
  servo2.write(90);  // 停止
  servo3.write(90);  // 中位
  
  Serial.println("✓ 舵机初始化完成");
  
  // 初始化电机引脚
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
  
  Serial.println("✓ 电机初始化完成");
  
  // 连接WiFi (支持多WiFi记忆)
  setupWiFi();
  
  // 启动Web服务器
  setupWebServer();
  
  // 启动WebSocket服务器
  setupWebSocket();

  lastCommandTime = millis();
  
  Serial.println("========================================");
  Serial.println("✓ 系统启动完成");
  Serial.print("✓ 访问地址: http://");
  Serial.println(WiFi.localIP());
  Serial.println("========================================\n");
}

// ========== 主循环 ==========
void loop() {
  // WebSocket处理
  webSocket.loop();
  
  // 检查各电机自动停止
  checkServo1AutoStop();
  checkServo2AutoStop();
  checkMotorAutoStop();
  
  // 发送心跳
  sendHeartbeatIfNeeded();
  
  // LED状态控制（配网模式优先）
  if (configModeActive) {
    // 配网模式：快闪（200ms间隔）
    blinkLED(200);
  } else if (WiFi.status() != WL_CONNECTED) {
    // 没网络：快闪（200ms间隔）
    blinkLED(200);
  } else {
    // 正常工作：慢闪（2秒间隔）
    blinkLED(2000);
  }
  
  // WiFi状态检查（每5秒）
  if (millis() - lastWiFiCheck >= 5000) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi断开，尝试重连...");
      
      // 尝试重新连接已保存的WiFi
      if (wifiMulti.run(WIFI_CONNECT_TIMEOUT) == WL_CONNECTED) {
        Serial.println("✓ WiFi重连成功");
        Serial.print("✓ 连接到: ");
        Serial.println(WiFi.SSID());
      }
    }
    lastWiFiCheck = millis();
  }

  // 指令超时进入深度睡眠
  checkInactivitySleep();
}

// ========== WiFi连接（支持多WiFi记忆） ==========
void setupWiFi() {
  // 初始化Preferences
  preferences.begin("wifi-config", false);
  
  Serial.println("\n--- WiFi配置 ---");
  
  // 加载已保存的WiFi列表
  int networkCount = loadWiFiCredentials();
  
  if (networkCount > 0) {
    Serial.printf("发现 %d 个已保存的WiFi网络\n", networkCount);
    printSavedNetworks();
    
    // 尝试连接已保存的WiFi
    Serial.println("正在尝试连接已保存的WiFi...");
    
    if (connectToSavedWiFi()) {
      Serial.println("✓ 成功连接到已保存的WiFi");
      Serial.print("✓ 连接到: ");
      Serial.println(WiFi.SSID());
      Serial.print("✓ IP地址: ");
      Serial.println(WiFi.localIP());
      Serial.print("✓ 信号强度: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      return;
    }
  }
  
  // 所有已保存的WiFi都连接失败，启动配网模式
  Serial.println("⚠️ 无法连接到已保存的WiFi，启动配网模式...");
  
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setAPCallback(configModeCallback);
  
  String apName = "ESP32-RobotArm-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  
  Serial.print("配网热点名称: ");
  Serial.println(apName);
  
  // 进入配网模式
  if (!wifiManager.autoConnect(apName.c_str())) {
    Serial.println("✗ 配网超时，重启设备...");
    delay(3000);
    ESP.restart();
  }
  ledTicker.detach();
  digitalWrite(LED_PIN, LOW);
  configModeActive = false;
  
  // 配网成功，保存新的WiFi信息
  String newSSID = WiFi.SSID();
  String newPassword = WiFi.psk();
  
  Serial.println("\n✓ WiFi配网成功");
  Serial.print("✓ 连接到: ");
  Serial.println(newSSID);
  Serial.print("✓ IP地址: ");
  Serial.println(WiFi.localIP());
  Serial.print("✓ 信号强度: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  
  // 保存WiFi凭证
  saveWiFiCredentials(newSSID, newPassword);
  
  // 添加到WiFiMulti
  wifiMulti.addAP(newSSID.c_str(), newPassword.c_str());
}

// ========== 保存WiFi凭证 ==========
void saveWiFiCredentials(String ssid, String password) {
  // 读取当前保存的网络数量
  int count = preferences.getInt("count", 0);
  
  // 检查是否已存在
  for (int i = 0; i < count; i++) {
    String savedSSID = preferences.getString(("ssid" + String(i)).c_str(), "");
    if (savedSSID == ssid) {
      Serial.println("ℹ️ WiFi已存在，更新密码");
      preferences.putString(("pass" + String(i)).c_str(), password);
      return;
    }
  }
  
  // 新增WiFi
  if (count < MAX_WIFI_NETWORKS) {
    preferences.putString(("ssid" + String(count)).c_str(), ssid);
    preferences.putString(("pass" + String(count)).c_str(), password);
    preferences.putInt("count", count + 1);
    Serial.printf("✓ 已保存WiFi: %s (%d/%d)\n", ssid.c_str(), count + 1, MAX_WIFI_NETWORKS);
  } else {
    // 达到上限，覆盖最旧的（索引0）
    Serial.println("⚠️ WiFi列表已满，移除最旧的网络");
    
    // 所有网络前移
    for (int i = 0; i < MAX_WIFI_NETWORKS - 1; i++) {
      String ssid = preferences.getString(("ssid" + String(i + 1)).c_str(), "");
      String pass = preferences.getString(("pass" + String(i + 1)).c_str(), "");
      preferences.putString(("ssid" + String(i)).c_str(), ssid);
      preferences.putString(("pass" + String(i)).c_str(), pass);
    }
    
    // 在最后位置保存新WiFi
    preferences.putString(("ssid" + String(MAX_WIFI_NETWORKS - 1)).c_str(), ssid);
    preferences.putString(("pass" + String(MAX_WIFI_NETWORKS - 1)).c_str(), password);
    Serial.printf("✓ 已保存WiFi: %s\n", ssid.c_str());
  }
}

// ========== 加载WiFi凭证 ==========
int loadWiFiCredentials() {
  int count = preferences.getInt("count", 0);
  
  for (int i = 0; i < count; i++) {
    String ssid = preferences.getString(("ssid" + String(i)).c_str(), "");
    String password = preferences.getString(("pass" + String(i)).c_str(), "");
    
    if (ssid.length() > 0) {
      wifiMulti.addAP(ssid.c_str(), password.c_str());
    }
  }
  
  return count;
}

// ========== 连接到已保存的WiFi ==========
bool connectToSavedWiFi() {
  // 使用WiFiMulti自动选择信号最强的WiFi
  unsigned long startTime = millis();
  
  while (millis() - startTime < 30000) {  // 最多尝试30秒
    if (wifiMulti.run(WIFI_CONNECT_TIMEOUT) == WL_CONNECTED) {
      return true;
    }
    delay(100);
  }
  
  return false;
}

// ========== 打印已保存的网络 ==========
void printSavedNetworks() {
  int count = preferences.getInt("count", 0);
  Serial.println("--- 已保存的WiFi网络 ---");
  for (int i = 0; i < count; i++) {
    String ssid = preferences.getString(("ssid" + String(i)).c_str(), "");
    if (ssid.length() > 0) {
      Serial.printf("  %d. %s\n", i + 1, ssid.c_str());
    }
  }
  Serial.println("-------------------------");
}

// ========== 配网模式回调 ==========
void configModeCallback(WiFiManager *myWiFiManager) {
  configModeActive = true;
  ledTicker.attach_ms(200, toggleLed);
  Serial.println("\n========================================");
  Serial.println("📡 进入配网模式");
  Serial.print("配网热点: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.print("配网IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("请连接此热点并打开浏览器配置WiFi");
  Serial.println("========================================");
}

// ========== LED闪烁 ==========
void blinkLED(int interval) {
  if (millis() - lastLedBlink >= interval) {
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    lastLedBlink = millis();
  }
}

void toggleLed() {
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState);
}

// ========== Web服务器设置 ==========
void setupWebServer() {
  // 提供静态文件
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });
  
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });
  
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/script.js", "application/javascript");
  });
  
  // 重置WiFi配置
  server.on("/reset-wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "所有WiFi配置已清除，设备将重启进入配网模式...");
    delay(1000);
    
    // 清除所有保存的WiFi
    preferences.clear();
    
    // 清除WiFiManager配置
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    
    delay(1000);
    ESP.restart();
  });
  
  // 获取WiFi信息
  server.on("/wifi-info", HTTP_GET, [](AsyncWebServerRequest *request){
    StaticJsonDocument<512> doc;
    
    doc["connected"] = (WiFi.status() == WL_CONNECTED);
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    
    // 已保存的WiFi列表
    JsonArray saved = doc.createNestedArray("saved_networks");
    int count = preferences.getInt("count", 0);
    for (int i = 0; i < count; i++) {
      String ssid = preferences.getString(("ssid" + String(i)).c_str(), "");
      if (ssid.length() > 0) {
        saved.add(ssid);
      }
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });
  
  // 启动服务器
  server.begin();
  Serial.println("✓ Web服务器启动 (端口80)");
}

// ========== WebSocket设置 ==========
void setupWebSocket() {
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("✓ WebSocket服务器启动 (端口81)");
}

// ========== WebSocket事件处理 ==========
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] 客户端断开\n", num);
      break;
      
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] 客户端连接: %s\n", num, ip.toString().c_str());
      }
      break;
      
    case WStype_TEXT:
      handleWebSocketMessage(num, payload, length);
      break;
  }
}

// ========== 处理WebSocket消息 ==========
void handleWebSocketMessage(uint8_t num, uint8_t* payload, size_t length) {
  // 解析JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.println("✗ JSON解析失败");
    return;
  }

  lastCommandTime = millis();
  
  String type = doc["type"];
  
  // 处理电机控制指令
  String motor = doc["motor"];
  
  if (type == "run_duration") {
    // 短按：定时运行
    int speed = doc["speed"];
    int duration = doc["duration"];
    
    if (motor == "servo1") {
      startServo1(speed, duration, true);
      Serial.printf("🔄 舵机1: 速度%d, 运行%dms\n", speed, duration);
    } 
    else if (motor == "servo2") {
      startServo2(speed, duration, true);
      Serial.printf("🔄 舵机2: 速度%d, 运行%dms\n", speed, duration);
    }
    else if (motor == "motor") {
      bool forward = doc["forward"];
      startMotor(speed, duration, true, forward);
      Serial.printf("🔄 电机: 速度%d, 运行%dms, %s\n", speed, duration, forward ? "正转" : "反转");
    }
  }
  else if (type == "start_continuous") {
    // 长按：持续运行
    int speed = doc["speed"];
    
    if (motor == "servo1") {
      startServo1(speed, 0, false);
      Serial.printf("▶️ 舵机1: 持续运行，速度%d\n", speed);
    }
    else if (motor == "servo2") {
      startServo2(speed, 0, false);
      Serial.printf("▶️ 舵机2: 持续运行，速度%d\n", speed);
    }
    else if (motor == "motor") {
      bool forward = doc["forward"];
      startMotor(speed, 0, false, forward);
      Serial.printf("▶️ 电机: 持续运行，速度%d, %s\n", speed, forward ? "正转" : "反转");
    }
  }
  else if (type == "stop") {
    // 停止
    if (motor == "servo1") {
      stopServo1();
      Serial.println("⏹️ 舵机1停止");
    }
    else if (motor == "servo2") {
      stopServo2();
      Serial.println("⏹️ 舵机2停止");
    }
    else if (motor == "motor") {
      stopMotor();
      Serial.println("⏹️ 电机停止");
    }
  }
  else if (type == "servo180") {
    // 180度舵机
    int angle = doc["angle"];
    setServo3Angle(angle);
    Serial.printf("📐 180度舵机: %d°\n", angle);
  }
}

// ========== 检查舵机1自动停止 ==========
void checkServo1AutoStop() {
  if (servo1State.running && servo1State.autoStop) {
    if (millis() - servo1State.startTime >= servo1State.duration) {
      stopServo1();
      Serial.println("⏹️ 舵机1自动停止");
    }
  }
}

// ========== 检查舵机2自动停止 ==========
void checkServo2AutoStop() {
  if (servo2State.running && servo2State.autoStop) {
    if (millis() - servo2State.startTime >= servo2State.duration) {
      stopServo2();
      Serial.println("⏹️ 舵机2自动停止");
    }
  }
}

// ========== 检查电机自动停止 ==========
void checkMotorAutoStop() {
  if (motorState.running && motorState.autoStop) {
    if (millis() - motorState.startTime >= motorState.duration) {
      stopMotor();
      Serial.println("⏹️ 电机自动停止");
    }
  }
}

// ========== 发送心跳 ==========
void sendHeartbeatIfNeeded() {
  if (millis() - lastHeartbeat >= 1000) {
    sendHeartbeat();
    lastHeartbeat = millis();
  }
}

void sendHeartbeat() {
  if (webSocket.connectedClients() == 0) return;
  
  StaticJsonDocument<256> doc;
  doc["type"] = "heartbeat";
  doc["timestamp"] = millis();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["ssid"] = WiFi.SSID();
  
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

// ========== 舵机1控制 ==========
void startServo1(int speed, int duration, bool autoStop) {
  servo1.write(speed);
  servo1State.speed = speed;
  servo1State.startTime = millis();
  servo1State.duration = duration;
  servo1State.running = true;
  servo1State.autoStop = autoStop;
}

void stopServo1() {
  servo1.write(90);
  servo1State.running = false;
}

// ========== 舵机2控制 ==========
void startServo2(int speed, int duration, bool autoStop) {
  servo2.write(speed);
  servo2State.speed = speed;
  servo2State.startTime = millis();
  servo2State.duration = duration;
  servo2State.running = true;
  servo2State.autoStop = autoStop;
}

void stopServo2() {
  servo2.write(90);
  servo2State.running = false;
}

// ========== 电机控制 ==========
void startMotor(int speed, int duration, bool autoStop, bool forward) {
  if (forward) {
    analogWrite(MOTOR_IN1, speed);
    analogWrite(MOTOR_IN2, 0);
  } else {
    analogWrite(MOTOR_IN1, 0);
    analogWrite(MOTOR_IN2, speed);
  }
  
  motorState.speed = speed;
  motorState.startTime = millis();
  motorState.duration = duration;
  motorState.running = true;
  motorState.autoStop = autoStop;
}

void stopMotor() {
  analogWrite(MOTOR_IN1, 0);
  analogWrite(MOTOR_IN2, 0);
  motorState.running = false;
}

// ========== 180度舵机控制 ==========
void setServo3Angle(int angle) {
  servo3.write(angle);
}

// ========== 指令超时休眠 ==========
void checkInactivitySleep() {
  if (configModeActive) return;

  if (millis() - lastCommandTime >= COMMAND_IDLE_TIMEOUT_MS) {
    Serial.println("💤 5分钟无指令，进入深度睡眠");
    enterDeepSleep();
  }
}

void enterDeepSleep() {
  // 停止所有电机/舵机
  stopServo1();
  stopServo2();
  stopMotor();
  servo3.write(90);

  // 关闭网络服务
  webSocket.close();
  server.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  digitalWrite(LED_PIN, LOW);
  delay(100);

  // 进入深度睡眠（需外部复位/唤醒）
  esp_deep_sleep_start();
}
