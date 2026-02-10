// 動作確認済みバージョン (Once it works!)
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <vector>

// --- LittleFS for serving HTML ---
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WebSocketsServer.h>

WebServer server(80);
WebSocketsServer webSocket(81);
DNSServer dnsServer;
const byte DNS_PORT = 53;
Preferences preferences;

Servo servo1, servo2, servo3;
const int PIN_SERVO1 = 25;
const int PIN_SERVO2 = 26;
const int PIN_SERVO3 = 27;

// --- WS2812B LED設定 ---
#define LED_COUNT 35
#define LED_PIN 13
Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
bool ledManualMode = false;   // true=ユーザー手動操作中, false=ステート連動
uint32_t currentLedColor = 0; // 現在の色 (Manual時保持用)

void updateLed(uint32_t color) {
  for (int i = 0; i < pixels.numPixels(); i++) {
    pixels.setPixelColor(i, color);
  }
  pixels.show();
}

// --- サーボ設定 ---
const int US_AT_0_DEG = 500;    // 0度 (閉/強)
const int US_AT_270_DEG = 2500; // 270度 (開/弱)
const unsigned long DETACH_DELAY_MS = 5000;

// --- 位置補正（個別サーボオフセット） ---
int servo1Offset = 0; // -90 ~ +90の範囲で調整可能
int servo2Offset = 0;
int servo3Offset = 0;

// --- 角度制限 (最小/最大) ---
int minAngle1 = 0;
int maxAngle1 = 270;
int minAngle2 = 0;
int maxAngle2 = 270;
int minAngle3 = 0;
int maxAngle3 = 270;

// --- HC-SR04センサー ---
const int TRIG_PIN = 32;
const int ECHO_PIN = 33;
float currentDistance = 0.0;
unsigned long lastDistanceMeasure = 0;

// 前方宣言
void setServoAngleSafe(int servoNum, int targetAngle);

bool sensorEnabled = false;
float sensorThreshold = 10.0; // Default 10cm
int sensorTriggerCount = 0;   // 連続検知カウンタ

// --- 履歴構造体 ---
struct HistoryItem {
  String timeStr;
  String preset;
  int strength;
  int count;
};
std::vector<HistoryItem> historyLog;
String currentSessionPreset = "Custom";

// --- 履歴の永続化 ---
void loadHistoryFromFile() {
  File file = LittleFS.open("/history.json", "r");
  if (!file) {
    Serial.println("No history file found, starting fresh");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.printf("History JSON parse error: %s\n", error.c_str());
    return;
  }

  historyLog.clear();
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject item : arr) {
    HistoryItem h;
    h.timeStr = item["time"].as<String>();
    h.preset = item["preset"].as<String>();
    h.strength = item["strength"] | 0;
    h.count = item["count"] | 0;
    historyLog.push_back(h);
  }
  Serial.printf("Loaded %d history items\n", historyLog.size());
}

void saveHistoryToFile() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();

  for (const auto &h : historyLog) {
    JsonObject item = arr.add<JsonObject>();
    item["time"] = h.timeStr;
    item["preset"] = h.preset;
    item["strength"] = h.strength;
    item["count"] = h.count;
  }

  File file = LittleFS.open("/history.json", "w");
  if (!file) {
    Serial.println("Failed to open history file for writing");
    return;
  }
  serializeJson(doc, file);
  file.close();
  Serial.printf("Saved %d history items\n", historyLog.size());
}

// 状態管理
// 状態管理
/*
enum State {
  IDLE,
  PREPARE_SQUEEZE,
  SQUEEZING,
  HOLDING,
  RELEASING,
  WAIT_CYCLE,
  FINISHED_BLINK
};
*/
// 状態定義
// - IDLE: 待機中
// - PREPARE_SQUEEZE: 締め付け準備中
// - SQUEEZING: 締め付け中
// - HOLDING: 締め付け保持中
// - RELEASING: 緩め中
// - WAIT_CYCLE: 次のサイクル待機中
// - FINISHED_BLINK: 完了点滅中 (IDLEではないので動作中扱い)
const int IDLE = 0;
const int PREPARE_SQUEEZE = 1;
const int SQUEEZING = 2;
const int HOLDING = 3;
const int RELEASING = 4;
const int WAIT_CYCLE = 5;
const int FINISHED_BLINK = 6;

int currentState = IDLE;
int lastState = IDLE;

unsigned long stateStartTime = 0;
unsigned long sessionStartTime = 0;

// パラメータ
float holdTimeSec = 0.5;
float reachTimeSec = 0.5;

int targetStrength = 90; // Default to Normal (v2.9+)
int targetCount = 3;

// ★追加: グラデーション用変数
bool gradationEnabled = false;
int gradationStart = 50;
int gradationEnd = 100;

int currentCycle = 0;
int pin13State = 0;
int activeLedCount = 35;

void setAllServosAngle(int angle) {
  setServoAngleSafe(1, angle);
  setServoAngleSafe(2, angle);
  setServoAngleSafe(3, angle);
}

int strengthToAngle(int strength) {
  if (strength < 0)
    strength = 0;
  if (strength > 100)
    strength = 100;
  // 270度(開) → 90度(最大閉) の範囲に制限
  return map(strength, 0, 100, 270, 90);
}

void attachAllServos() {
  if (!servo1.attached())
    servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG);
  if (!servo2.attached())
    servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG);
  if (!servo3.attached())
    servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG);
}

void detachAllServos() {
  if (servo1.attached())
    servo1.detach();
  if (servo2.attached())
    servo2.detach();
  if (servo3.attached())
    servo3.detach();
}

// 安全なサーボ制御関数 (制限とオフセット適用)
void setServoAngleSafe(int servoNum, int targetAngle) {
  int minA = 0;
  int maxA = 270;
  int offset = 0;

  if (servoNum == 1) {
    minA = minAngle1;
    maxA = maxAngle1;
    offset = servo1Offset;
  } else if (servoNum == 2) {
    minA = minAngle2;
    maxA = maxAngle2;
    offset = servo2Offset;
  } else if (servoNum == 3) {
    minA = minAngle3;
    maxA = maxAngle3;
    offset = servo3Offset;
  } else {
    return; // Invalid servo number
  }

  // 1. 角度制限 (最優先)
  if (targetAngle < minA)
    targetAngle = minA;
  if (targetAngle > maxA)
    targetAngle = maxA;

  // 2. オフセット適用
  // 補正: 目標角度 - オフセット
  int correctedAngle = targetAngle - offset;

  // 3. 物理限界リミット
  if (correctedAngle < 0)
    correctedAngle = 0;
  if (correctedAngle > 270)
    correctedAngle = 270;

  // 4. 出力
  int us = map(correctedAngle, 0, 270, US_AT_0_DEG, US_AT_270_DEG);
  Serial.printf("S%d->%d(%dus)\n", servoNum, correctedAngle, us); // Debug

  if (servoNum == 1) {
    if (!servo1.attached())
      servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG);
    servo1.writeMicroseconds(us);
  } else if (servoNum == 2) {
    if (!servo2.attached())
      servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG);
    servo2.writeMicroseconds(us);
  } else if (servoNum == 3) {
    if (!servo3.attached())
      servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG);
    servo3.writeMicroseconds(us);
  }
}

// Helper for raw read
float readRawDistance() {
  digitalWrite(32, LOW); // TRIG
  delayMicroseconds(2);
  digitalWrite(32, HIGH);
  delayMicroseconds(10);
  digitalWrite(32, LOW);

  long duration =
      pulseIn(33, HIGH, 3000); // 3ms timeout (approx 50cm) to fix lag
  if (duration == 0)
    return 999.0;
  return duration * 0.034 / 2.0;
}

float measureDistance() {
  // Enhanced Median Filter (Reduced to 3 samples for speed)
  float readings[3];
  for (int i = 0; i < 3; i++) {
    readings[i] = readRawDistance();
    delay(1); // Reduced delay
  }

  // Simple Bubble Sort
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2 - i; j++) {
      if (readings[j] > readings[j + 1]) {
        float temp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = temp;
      }
    }
  }

  // Return Median
  return readings[1];
}

// --- WebSocket logic ---
void broadcastStatus() {
  static unsigned long lastBroadcast = 0;
  if (millis() - lastBroadcast < 200)
    return;
  lastBroadcast = millis();

  JsonDocument doc;
  const char *stateStr;
  switch (currentState) {
  case IDLE:
    stateStr = "IDLE";
    break;
  case PREPARE_SQUEEZE:
    stateStr = "PREPARE_SQUEEZE";
    break;
  case SQUEEZING:
    stateStr = "SQUEEZING";
    break;
  case HOLDING:
    stateStr = "HOLDING";
    break;
  case RELEASING:
    stateStr = "RELEASING";
    break;
  case WAIT_CYCLE:
    stateStr = "WAIT_CYCLE";
    break;
  case FINISHED_BLINK:
    stateStr = "FINISHED_BLINK";
    break;
  default:
    stateStr = "UNKNOWN";
  }

  float cycleDur = reachTimeSec + holdTimeSec + 0.5;
  float totalDur = 0.3 + (targetCount * cycleDur);
  unsigned long elap =
      (currentState == IDLE) ? 0 : (millis() - sessionStartTime);
  float progress = (float)elap / (totalDur * 1000.0);
  if (progress > 1.0)
    progress = 1.0;

  doc["type"] = "status";
  doc["state"] = stateStr;
  doc["cycle"] = currentCycle;
  doc["total"] = targetCount;
  doc["prog"] = (int)(progress * 100);
  doc["rem"] = (totalDur > (elap / 1000.0)) ? (totalDur - (elap / 1000.0)) : 0;

  String output;
  serializeJson(doc, output);
  webSocket.broadcastTXT(output);
}

void sendInit(uint8_t num) {
  JsonDocument doc;
  doc["type"] = "init";
  doc["str"] = targetStrength;
  doc["cnt"] = targetCount;
  doc["hold"] = holdTimeSec;
  doc["reach"] = reachTimeSec;
  doc["grad_en"] = gradationEnabled;
  doc["g_start"] = gradationStart;
  doc["g_end"] = gradationEnd;
  doc["sensor"] = sensorEnabled;
  doc["sth"] = sensorThreshold;
  doc["led_cnt"] = activeLedCount;
  doc["preset"] = currentSessionPreset;
  doc["build"] = "2026-02-10 16:30";

  doc["off1"] = servo1Offset;
  doc["off2"] = servo2Offset;
  doc["off3"] = servo3Offset;
  doc["min1"] = minAngle1;
  doc["max1"] = maxAngle1;
  doc["min2"] = minAngle2;
  doc["max2"] = maxAngle2;
  doc["min3"] = minAngle3;
  doc["max3"] = maxAngle3;

  String output;
  serializeJson(doc, output);
  webSocket.sendTXT(num, output);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload,
                      size_t length) {
  switch (type) {
  case WStype_DISCONNECTED:
    Serial.printf("[%u] Disconnected!\n", num);
    break;
  case WStype_CONNECTED: {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2],
                  ip[3]);
    sendInit(num);
  } break;
  case WStype_TEXT: {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
      return;

    String cmd = doc["cmd"] | "";
    if (cmd == "start") {
      targetStrength = doc["str"] | targetStrength;
      targetCount = doc["cnt"] | targetCount;
      currentSessionPreset = doc["preset"] | "カスタム";
      gradationEnabled = doc["grad_en"] | false;
      gradationStart = doc["g_start"] | gradationStart;
      gradationEnd = doc["g_end"] | gradationEnd;

      preferences.putInt("str", targetStrength);
      preferences.putInt("cnt", targetCount);

      currentCycle = 0;
      sessionStartTime = millis();
      currentState = PREPARE_SQUEEZE;
      Serial.println("WS Start");
    } else if (cmd == "stop") {
      currentState = IDLE;
      attachAllServos();
      setAllServosAngle(270);
      Serial.println("WS Stop");
    } else if (cmd == "set") {
      if (doc.containsKey("hold")) {
        holdTimeSec = doc["hold"];
        preferences.putFloat("hold", holdTimeSec);
      }
      if (doc.containsKey("reach")) {
        reachTimeSec = doc["reach"];
        preferences.putFloat("reach", reachTimeSec);
      }
      if (doc.containsKey("sth")) {
        sensorThreshold = doc["sth"];
        preferences.putFloat("sth", sensorThreshold);
      }
      if (doc.containsKey("sensor")) {
        sensorEnabled = doc["sensor"];
        preferences.putBool("sensor", sensorEnabled);
      }
      if (doc.containsKey("led_cnt")) {
        activeLedCount = doc["led_cnt"];
        preferences.putInt("led_cnt", activeLedCount);
      }
      Serial.println("WS Set");
    } else if (cmd == "manual") {
      int pct = doc["val"] | 0;
      int targetAngle = strengthToAngle(pct);
      currentState = IDLE;
      stateStartTime = millis();
      attachAllServos();
      setAllServosAngle(targetAngle);
      Serial.printf("WS Manual: %d%%\n", pct);
    } else if (cmd == "led_mode") {
      ledManualMode = !(doc["auto"] | true);
      Serial.printf("WS LED Mode: %s\n", ledManualMode ? "Manual" : "Auto");
    } else if (cmd == "led_color") {
      ledManualMode = true;
      if (doc.containsKey("hex")) {
        String hex = doc["hex"].as<String>();
        if (hex.startsWith("#"))
          hex = hex.substring(1);
        long n = strtol(hex.c_str(), NULL, 16);
        currentLedColor =
            pixels.Color((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF);
      } else {
        currentLedColor =
            pixels.Color(doc["r"] | 0, doc["g"] | 0, doc["b"] | 0);
      }
      updateLed(currentLedColor);
    } else if (cmd == "get_history") {
      JsonDocument hdoc;
      hdoc["type"] = "history";
      JsonArray arr = hdoc["data"].to<JsonArray>();
      for (const auto &h : historyLog) {
        JsonObject item = arr.add<JsonObject>();
        item["time"] = h.timeStr;
        item["preset"] = h.preset;
        item["strength"] = h.strength;
        item["count"] = h.count;
      }
      String output;
      serializeJson(hdoc, output);
      webSocket.sendTXT(num, output);
    } else if (cmd == "set_offset") {
      int s = doc["num"] | 0;
      int v = doc["val"] | 0;
      if (s >= 1 && s <= 3) {
        if (s == 1) {
          servo1Offset = v;
          preferences.putInt("servo1Off", v);
        } else if (s == 2) {
          servo2Offset = v;
          preferences.putInt("servo2Off", v);
        } else if (s == 3) {
          servo3Offset = v;
          preferences.putInt("servo3Off", v);
        }
        int correctedAngle = 270 - v;
        if (correctedAngle < 0)
          correctedAngle = 0;
        if (correctedAngle > 270)
          correctedAngle = 270;
        int us = map(correctedAngle, 0, 270, US_AT_0_DEG, US_AT_270_DEG);
        if (s == 1) {
          if (!servo1.attached())
            servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG);
          servo1.writeMicroseconds(us);
        } else if (s == 2) {
          if (!servo2.attached())
            servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG);
          servo2.writeMicroseconds(us);
        } else if (s == 3) {
          if (!servo3.attached())
            servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG);
          servo3.writeMicroseconds(us);
        }
      }
    } else if (cmd == "set_limit") {
      int s = doc["num"] | 0;
      int mi = doc["min"] | 0;
      int ma = doc["max"] | 270;
      if (s == 1) {
        minAngle1 = mi;
        maxAngle1 = ma;
        preferences.putInt("minAng1", mi);
        preferences.putInt("maxAng1", ma);
      } else if (s == 2) {
        minAngle2 = mi;
        maxAngle2 = ma;
        preferences.putInt("minAng2", mi);
        preferences.putInt("maxAng2", ma);
      } else if (s == 3) {
        minAngle3 = mi;
        maxAngle3 = ma;
        preferences.putInt("minAng3", mi);
        preferences.putInt("maxAng3", ma);
      }
    }
  } break;
  default:
    break;
  }
}

void handleRoot() {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    server.send(500, "text/plain", "Failed to open file");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void setup() {
  Serial.begin(115200);

  // LittleFS initialization
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }

  // Load history from file
  loadHistoryFromFile();

  preferences.begin("job", false);
  holdTimeSec = preferences.getFloat("hold", 0.5);
  reachTimeSec = preferences.getFloat("reach", 0.5);
  pin13State = preferences.getInt("pin13", 0);
  activeLedCount = preferences.getInt("led_cnt", 35);
  servo1Offset = preferences.getInt("servo1Off", 0);
  servo2Offset = preferences.getInt("servo2Off", 0);
  servo3Offset = preferences.getInt("servo3Off", 0);

  sensorEnabled = preferences.getBool("sensor", false);
  sensorThreshold = preferences.getFloat("sth", 10.0);
  targetStrength = preferences.getInt("str", 90);
  targetCount = preferences.getInt("cnt", 3);

  minAngle1 = preferences.getInt("minAng1", 0);
  maxAngle1 = preferences.getInt("maxAng1", 270);
  minAngle2 = preferences.getInt("minAng2", 0);
  maxAngle2 = preferences.getInt("maxAng2", 270);
  minAngle3 = preferences.getInt("minAng3", 0);
  maxAngle3 = preferences.getInt("maxAng3", 270);

  // HC-SR04センサー設定
  pinMode(32, OUTPUT); // Trig
  pinMode(33, INPUT);  // Echo

  pinMode(13, OUTPUT);
  // digitalWrite(13, pin13State ? HIGH : LOW);

  // NeoPixel Init
  pixels.begin();
  pixels.setBrightness(50); // 適度な明るさ
  pixels.show();            // Initialize all pixels to 'off'

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servo1.setPeriodHertz(50);
  servo2.setPeriodHertz(50);
  servo3.setPeriodHertz(50);

  // attachAllServos();
  Serial.printf("Servo1 attached: %d\n",
                servo1.attach(PIN_SERVO1, US_AT_0_DEG, US_AT_270_DEG));
  Serial.printf("Servo2 attached: %d\n",
                servo2.attach(PIN_SERVO2, US_AT_0_DEG, US_AT_270_DEG));
  Serial.printf("Servo3 attached: %d\n",
                servo3.attach(PIN_SERVO3, US_AT_0_DEG, US_AT_270_DEG));

  setAllServosAngle(270); // 初期位置

  // --- AP Mode Setup (Yakisoba-Shiro) ---
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("焼きそば四郎"); // No Password

  Serial.print("AP IPs: ");
  Serial.println(WiFi.softAPIP());

  // DNS Server (Captive Portal)
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", handleRoot);
  // WebSocket handle (not needed for arduinoWebSockets as it's a separate
  // server)

  // Captive Portal Redirect
  server.onNotFound([]() { handleRoot(); });

  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);

  server.begin();
  Serial.println("Ready.");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
  webSocket.loop();

  if (currentState != IDLE) {
    broadcastStatus();
  }

  unsigned long now = millis();

  // HC-SR04センサー測定 (200msごと)
  if (now - lastDistanceMeasure > 200) {
    currentDistance = measureDistance();
    lastDistanceMeasure = now;

    if (currentState ==
        IDLE) { // Only check if IDLE to prevent lag during operation? User
                // wants button fix -> Optimization done in measureDistance
      if (sensorEnabled && currentDistance < sensorThreshold &&
          currentDistance > 0.1) {
        sensorTriggerCount++;
        Serial.printf("Sensor Detect: %.1f cm (Count: %d)\n", currentDistance,
                      sensorTriggerCount);

        // 3回連続検知 (approx 600ms) で開始
        if (sensorTriggerCount >= 3) {
          Serial.println(">>> Sensor START Triggered");
          sensorTriggerCount = 0;
          currentCycle = 0;
          sessionStartTime = millis();
          currentState = PREPARE_SQUEEZE;
          currentSessionPreset = "センサー自動";
        }
      } else {
        sensorTriggerCount = 0;
      }
    }
  }

  if (currentState != lastState) {
    stateStartTime = now;
    Serial.printf("State: %d\n", currentState);

    if (currentState == PREPARE_SQUEEZE) {
      attachAllServos();
      setAllServosAngle(270);
    }
    lastState = currentState;
  }

  // --- LED Auto Control ---
  static int lastLedState = -1;
  static bool lastLedManualMode = false;
  static uint32_t lastLedManualColor = 0;

  // Manual Mode Change Check or State Change or Manual Color Change
  bool needLedUpdate = false;

  if (ledManualMode != lastLedManualMode)
    needLedUpdate = true;
  if (!ledManualMode && currentState != lastLedState)
    needLedUpdate = true;
  if (ledManualMode && lastLedManualColor != currentLedColor)
    needLedUpdate = true;

  // Force update during blink or ANY running state to allow animation
  if (currentState != IDLE && !ledManualMode)
    needLedUpdate = true;

  if (needLedUpdate) {
    if (ledManualMode) {
      updateLed(currentLedColor);
      lastLedManualColor = currentLedColor;
    } else {
      if (currentState == IDLE) {
        updateLed(0); // OFF
      } else if (currentState == FINISHED_BLINK) {
        unsigned long elapsed = millis() - stateStartTime;
        // Blink Logic: Faster! 200ms interval, 3 times
        // 0-200 ON, 200-400 OFF
        // 400-600 ON, 600-800 OFF
        // 800-1000 ON, >1000 OFF (Exit)
        bool on = false;
        if (elapsed < 200)
          on = true;
        else if (elapsed >= 400 && elapsed < 600)
          on = true;
        else if (elapsed >= 800 && elapsed < 1000)
          on = true;

        if (on)
          updateLed(pixels.Color(0, 255, 0)); // Green
        else
          updateLed(0); // OFF
      } else if (currentState == WAIT_CYCLE) {
        // WAIT_CYCLE: Turn off LEDs immediately to prepare for blink
        // This prevents the "all green" state between completion and blink
        updateLed(0); // OFF
      } else if (currentState == RELEASING && currentCycle >= targetCount) {
        // Last cycle RELEASING: Force 100% display
        // Note: currentCycle is incremented in RELEASING before this check
        for (int i = 0; i < LED_COUNT; i++) {
          if (i < activeLedCount) {
            pixels.setPixelColor(i, pixels.Color(0, 255, 0)); // All green
          } else {
            pixels.setPixelColor(i, 0);
          }
        }
        pixels.show();
      } else {
        // Progressive Green Bar
        // Calculate total duration = PREPARE(0.3s) + N * (Reach + Hold +
        // Release)
        float cycleDur = reachTimeSec + holdTimeSec + 0.3;
        float totalDur = 0.3 + ((float)targetCount * cycleDur);

        // Current elapsed in session
        unsigned long sessionElapsed = millis() - sessionStartTime;

        float progress = (float)sessionElapsed / (totalDur * 1000.0);
        if (progress > 1.0)
          progress = 1.0;
        if (progress < 0.0)
          progress = 0.0;

        int ledCount = (int)(progress * activeLedCount);
        for (int i = 0; i < LED_COUNT; i++) {
          if (i < activeLedCount) {
            if (i < ledCount)
              pixels.setPixelColor(i, pixels.Color(0, 255, 0));
            else
              pixels.setPixelColor(i, 0);
          } else {
            pixels.setPixelColor(i, 0);
          }
        }
        pixels.show();
      }
    }
    lastLedState = currentState;
    lastLedManualMode = ledManualMode;
  }

  switch (currentState) {
  case IDLE:
    yield();
    break;

  case PREPARE_SQUEEZE:
    setAllServosAngle(270);
    if (now - stateStartTime > 300) {
      currentState = SQUEEZING;
    }
    yield();
    break;

  case SQUEEZING: {
    unsigned long duration = reachTimeSec * 1000;
    unsigned long elapsed = now - stateStartTime;
    int startAngle = 270;

    // ★変更: 今回のサイクルの目標強度を計算
    int currentTargetStr = targetStrength; // デフォルトは固定強度

    if (gradationEnabled) {
      if (targetCount > 1) {
        // 線形補間: 開始値 + (終了値 - 開始値) * (現在の回数 / (総回数 - 1))
        // currentCycleは0から始まります (0, 1, 2...)
        float progress = (float)currentCycle / (float)(targetCount - 1);
        currentTargetStr =
            gradationStart + (int)((gradationEnd - gradationStart) * progress);
      } else {
        // 1回だけの場合は終了値を使う
        currentTargetStr = gradationEnd;
      }
    }

    // 計算した強度を角度に変換
    int targetAngle = strengthToAngle(currentTargetStr);

    if (elapsed >= duration) {
      setAllServosAngle(targetAngle);
      currentState = HOLDING;
    } else {
      float progress = (float)elapsed / (float)duration;
      int currentAngle = startAngle + (targetAngle - startAngle) * progress;
      setAllServosAngle(currentAngle);
    }
    yield();
  } break;

  case HOLDING:
    if (now - stateStartTime >= (holdTimeSec * 1000)) {
      currentState = RELEASING;
      currentCycle++; // カウントアップは状態遷移時のみ1回だけ！
    }
    yield();
    break;

  case RELEASING:
    setAllServosAngle(270);
    // currentCycle++ は削除 (HOLDINGで1回だけ実行済み)

    // Wait 500ms for physical servo return (180deg ~ 0.48s per datasheet)
    if (now - stateStartTime >= 500) {
      // Check if this was the last cycle
      if (currentCycle >= targetCount) {
        // Last cycle complete - transition to blink
        Serial.println("Finished.");
        currentState = FINISHED_BLINK;

        // --- 履歴保存 ---
        // getLocalTimeはネットがないと数秒フリーズするので削除
        String timeStr = String(millis() / 1000) + "秒前";

        HistoryItem newItem;
        newItem.timeStr = timeStr;
        newItem.preset = currentSessionPreset;
        newItem.strength = targetStrength;
        newItem.count = targetCount;

        historyLog.push_back(newItem);

        if (historyLog.size() > 20) {
          historyLog.erase(historyLog.begin());
        }

        // Save history to file
        saveHistoryToFile();
        // ----------------
      } else {
        // Not last cycle - go to WAIT_CYCLE
        currentState = WAIT_CYCLE;
      }
    }
    yield();
    break;

  case FINISHED_BLINK:
    if (now - stateStartTime >= 1000) {
      currentState = IDLE;
      setAllServosAngle(270); // Ensure Reset
    }
    yield();
    break;

  case WAIT_CYCLE:
    // Intermediate cycle - just transition to next SQUEEZING
    currentState = SQUEEZING;
    yield();
    break;
  }
}
