
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>
#include <vector>

static const int NUM_SERVOS    = 3;
static const int SERVO_PINS[NUM_SERVOS] = {25, 26, 27};
static const int US_AT_0_DEG   = 500;
static const int US_AT_270_DEG = 2500;

static const int LED_PIN   = 13;
static const int LED_COUNT = 35;

static const int  TRIG_PIN = 32;
static const int  ECHO_PIN = 33;

static const byte DNS_PORT = 53;

static const char* PREF_OFFSET_KEYS[NUM_SERVOS]    = {"servo1Off", "servo2Off", "servo3Off"};
static const char* PREF_MIN_ANGLE_KEYS[NUM_SERVOS] = {"minAng1",   "minAng2",   "minAng3"};
static const char* PREF_MAX_ANGLE_KEYS[NUM_SERVOS] = {"maxAng1",   "maxAng2",   "maxAng3"};

WebServer   server(80);
DNSServer   dnsServer;
Preferences preferences;

Servo servos[NUM_SERVOS];
int   servoOffsets[NUM_SERVOS] = {0,   0,   0};
int   minAngles[NUM_SERVOS]    = {0,   0,   0};
int   maxAngles[NUM_SERVOS]    = {270, 270, 270};

Adafruit_NeoPixel pixels(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
bool     ledManualMode   = false;
uint32_t currentLedColor = 0;

void updateLed(uint32_t color) {
  for (int i = 0; i < pixels.numPixels(); i++) {
    pixels.setPixelColor(i, color);
  }
  pixels.show();
}

enum State {
  IDLE = 0,
  PREPARE_SQUEEZE,
  SQUEEZING,
  HOLDING,
  RELEASING,
  WAIT_CYCLE,
  FINISHED_BLINK
};

State         currentState   = IDLE;
State         lastState      = IDLE;
unsigned long stateStartTime   = 0;
unsigned long sessionStartTime = 0;

float holdTimeSec  = 0.5f;
float reachTimeSec = 0.5f;
int   targetStrength = 90;
int   targetCount    = 3;
int   currentCycle   = 0;
int   activeLedCount = 35;
int   pin13State     = 0;

bool gradationEnabled = false;
int  gradationStart   = 50;
int  gradationEnd     = 100;

float         currentDistance     = 0.0f;
unsigned long lastDistanceMeasure = 0;
bool          sensorEnabled       = false;
float         sensorThreshold     = 10.0f;
int           sensorTriggerCount  = 0;

struct HistoryItem {
  String timeStr;
  String preset;
  int    strength;
  int    count;
};
std::vector<HistoryItem> historyLog;
String currentSessionPreset = "Custom";

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
  for (JsonObject item : doc.as<JsonArray>()) {
    HistoryItem h;
    h.timeStr  = item["time"].as<String>();
    h.preset   = item["preset"].as<String>();
    h.strength = item["strength"] | 0;
    h.count    = item["count"] | 0;
    historyLog.push_back(h);
  }
  Serial.printf("Loaded %d history items\n", historyLog.size());
}

void saveHistoryToFile() {
  JsonDocument doc;
  JsonArray    arr = doc.to<JsonArray>();
  for (const auto& h : historyLog) {
    JsonObject item  = arr.add<JsonObject>();
    item["time"]     = h.timeStr;
    item["preset"]   = h.preset;
    item["strength"] = h.strength;
    item["count"]    = h.count;
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

int strengthToAngle(int strength) {
  strength = constrain(strength, 0, 100);

  return map(strength, 0, 100, 270, 90);
}

void setServoAngleSafe(int idx, int targetAngle) {
  if (idx < 0 || idx >= NUM_SERVOS) return;

  targetAngle = constrain(targetAngle, minAngles[idx], maxAngles[idx]);

  int correctedAngle = constrain(targetAngle - servoOffsets[idx], 0, 270);

  int us = map(correctedAngle, 0, 270, US_AT_0_DEG, US_AT_270_DEG);
  Serial.printf("S%d->%d(%dus)\n", idx + 1, correctedAngle, us);

  if (!servos[idx].attached()) {
    servos[idx].attach(SERVO_PINS[idx], US_AT_0_DEG, US_AT_270_DEG);
  }
  servos[idx].writeMicroseconds(us);
}

void attachAllServos() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    if (!servos[i].attached()) {
      servos[i].attach(SERVO_PINS[i], US_AT_0_DEG, US_AT_270_DEG);
    }
  }
}

void detachAllServos() {
  for (int i = 0; i < NUM_SERVOS; i++) {
    if (servos[i].attached()) {
      servos[i].detach();
    }
  }
}

void setAllServosAngle(int angle) {
  for (int i = 0; i < NUM_SERVOS; i++) {
    setServoAngleSafe(i, angle);
  }
}

float readRawDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 3000);
  if (duration == 0) return 999.0f;
  return duration * 0.034f / 2.0f;
}

float measureDistance() {

  float readings[3];
  for (int i = 0; i < 3; i++) {
    readings[i] = readRawDistance();
    delay(1);
  }

  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2 - i; j++) {
      if (readings[j] > readings[j + 1]) {
        float tmp       = readings[j];
        readings[j]     = readings[j + 1];
        readings[j + 1] = tmp;
      }
    }
  }
  return readings[1];
}

void moveServosToAngle(int angle) {
  currentState   = IDLE;
  stateStartTime = millis();
  attachAllServos();
  setAllServosAngle(angle);
}

static const char* stateToString(State s) {
  switch (s) {
    case IDLE:            return "IDLE";
    case PREPARE_SQUEEZE: return "PREPARE_SQUEEZE";
    case SQUEEZING:       return "SQUEEZING";
    case HOLDING:         return "HOLDING";
    case RELEASING:       return "RELEASING";
    case WAIT_CYCLE:      return "WAIT_CYCLE";
    case FINISHED_BLINK:  return "FINISHED_BLINK";
    default:              return "UNKNOWN";
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

void handleApiStart() {
  if (server.hasArg("str"))
    targetStrength = constrain(server.arg("str").toInt(), 0, 100);
  if (server.hasArg("cnt"))
    targetCount = server.arg("cnt").toInt();
  currentSessionPreset = server.hasArg("preset") ? server.arg("preset") : "カスタム";

  if (server.hasArg("grad_start") && server.hasArg("grad_end")) {
    gradationEnabled = true;
    gradationStart   = constrain(server.arg("grad_start").toInt(), 0, 100);
    gradationEnd     = constrain(server.arg("grad_end").toInt(),   0, 100);
    Serial.printf("[API] Start Gradation: %d%% -> %d%%\n", gradationStart, gradationEnd);
  } else {
    gradationEnabled = false;
  }

  preferences.putInt("str", targetStrength);
  preferences.putInt("cnt", targetCount);
  Serial.printf("[API] Start: Str=%d%%, Cnt=%d, Preset=%s\n",
                targetStrength, targetCount, currentSessionPreset.c_str());

  currentCycle     = 0;
  sessionStartTime = millis();
  currentState     = PREPARE_SQUEEZE;
  server.send(200, "text/plain", "OK");
}

void handleApiStop() {
  Serial.println("[API] Stop");
  currentState = IDLE;
  attachAllServos();
  setAllServosAngle(270);
  server.send(200, "text/plain", "OK");
}

void handleApiSettings() {
  if (server.hasArg("led_cnt")) {
    activeLedCount = constrain(server.arg("led_cnt").toInt(), 1, 35);
    preferences.putInt("led_cnt", activeLedCount);
    Serial.printf("[API] LED Count: %d\n", activeLedCount);
  }
  if (server.hasArg("hold")) {
    holdTimeSec = server.arg("hold").toFloat();
    preferences.putFloat("hold", holdTimeSec);
  }
  if (server.hasArg("reach")) {
    reachTimeSec = server.arg("reach").toFloat();
    preferences.putFloat("reach", reachTimeSec);
  }
  if (server.hasArg("str")) {
    targetStrength = server.arg("str").toInt();
    preferences.putInt("str", targetStrength);
  }
  if (server.hasArg("cnt")) {
    targetCount = server.arg("cnt").toInt();
    preferences.putInt("cnt", targetCount);
  }
  if (server.hasArg("sth")) {
    sensorThreshold = server.arg("sth").toFloat();
    preferences.putFloat("sth", sensorThreshold);
  }

  JsonDocument doc;
  doc["hold"]    = holdTimeSec;
  doc["reach"]   = reachTimeSec;
  doc["pin13"]   = pin13State;
  doc["sensor"]  = sensorEnabled;
  doc["sth"]     = sensorThreshold;
  doc["str"]     = targetStrength;
  doc["cnt"]     = targetCount;
  doc["led_cnt"] = activeLedCount;
  doc["build"]   = "2026-02-05 16:51";
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleApiStatus() {
  float cycleDur = reachTimeSec + holdTimeSec + 0.5f;
  float totalDur = 0.3f + (targetCount * cycleDur);

  JsonDocument doc;
  doc["state"]  = stateToString(currentState);
  doc["cycle"]  = currentCycle;
  doc["total"]  = targetCount;
  doc["elap"]   = millis() - sessionStartTime;
  doc["pin13"]  = pin13State;
  doc["dur"]    = totalDur;
  doc["preset"] = currentSessionPreset;
  doc["str"]    = targetStrength;
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleApiManual() {
  if (server.hasArg("val")) {
    int pct         = constrain(server.arg("val").toInt(), 0, 100);
    int targetAngle = strengthToAngle(pct);
    moveServosToAngle(targetAngle);
    Serial.printf("[API] Manual: %d%% -> %d deg\n", pct, targetAngle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiManualAngle() {
  if (server.hasArg("angle")) {
    int angle = constrain(server.arg("angle").toInt(), 90, 270);
    moveServosToAngle(angle);
    Serial.printf("[API] Manual Angle: %d degrees\n", angle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiServoAll() {
  if (server.hasArg("angle")) {
    int angle = constrain(server.arg("angle").toInt(), 90, 270);
    moveServosToAngle(angle);
    Serial.printf("[API] All Servos: %d degrees\n", angle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiServoIndividual() {
  if (server.hasArg("servo") && server.hasArg("angle")) {
    int servoNum = server.arg("servo").toInt();
    int angle    = server.arg("angle").toInt();
    currentState   = IDLE;
    stateStartTime = millis();
    setServoAngleSafe(servoNum - 1, angle);
    Serial.printf("[API] Servo %d: %d degrees\n", servoNum, angle);
  }
  server.send(200, "text/plain", "OK");
}

void handleApiHistory() {
  JsonDocument doc;
  JsonArray    arr = doc.to<JsonArray>();
  for (const auto& h : historyLog) {
    JsonObject item  = arr.add<JsonObject>();
    item["time"]     = h.timeStr;
    item["preset"]   = h.preset;
    item["strength"] = h.strength;
    item["count"]    = h.count;
  }
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void handleApiServoOffset() {
  if (!server.hasArg("servo")) {
    server.send(400, "text/plain", "Missing servo parameter");
    return;
  }
  int servoNum = server.arg("servo").toInt();
  if (servoNum < 1 || servoNum > NUM_SERVOS) {
    server.send(400, "text/plain", "Error: servo 1-3");
    return;
  }
  int idx = servoNum - 1;

  if (server.hasArg("value")) {
    int value = server.arg("value").toInt();
    if (value < -90 || value > 90) {
      server.send(400, "text/plain", "Error: value -90~+90");
      return;
    }
    servoOffsets[idx] = value;
    preferences.putInt(PREF_OFFSET_KEYS[idx], value);
    setServoAngleSafe(idx, 270);
    Serial.printf("[API] Servo %d Offset: %d\n", servoNum, value);
    server.send(200, "text/plain", "OK");
  } else {
    JsonDocument doc;
    doc["offset"] = servoOffsets[idx];
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
  }
}

void handleApiServoLimit() {
  if (!server.hasArg("servo")) {
    server.send(400, "text/plain", "Missing servo param");
    return;
  }
  int servoNum = server.arg("servo").toInt();
  if (servoNum < 1 || servoNum > NUM_SERVOS) {
    server.send(400, "text/plain", "Error: servo 1-3");
    return;
  }
  int idx = servoNum - 1;

  if (server.hasArg("min") && server.hasArg("max")) {
    minAngles[idx] = constrain(server.arg("min").toInt(), 0, 270);
    maxAngles[idx] = constrain(server.arg("max").toInt(), 0, 270);
    preferences.putInt(PREF_MIN_ANGLE_KEYS[idx], minAngles[idx]);
    preferences.putInt(PREF_MAX_ANGLE_KEYS[idx], maxAngles[idx]);
    server.send(200, "text/plain", "OK");
  } else {
    JsonDocument doc;
    doc["min"] = minAngles[idx];
    doc["max"] = maxAngles[idx];
    String output;
    serializeJson(doc, output);
    server.send(200, "application/json", output);
  }
}

void handleApiLed() {
  int r = 0, g = 0, b = 0;
  if (server.hasArg("color")) {
    String hex = server.arg("color");
    if (hex.startsWith("#")) hex = hex.substring(1);
    long number = strtol(hex.c_str(), nullptr, 16);
    r = (number >> 16) & 0xFF;
    g = (number >>  8) & 0xFF;
    b =  number        & 0xFF;
  } else if (server.hasArg("r") && server.hasArg("g") && server.hasArg("b")) {
    r = server.arg("r").toInt();
    g = server.arg("g").toInt();
    b = server.arg("b").toInt();
  }
  ledManualMode   = true;
  currentLedColor = pixels.Color(r, g, b);
  updateLed(currentLedColor);
  Serial.printf("[API] LED Manual: R%d G%d B%d\n", r, g, b);
  server.send(200, "text/plain", "OK");
}

void handleApiLedMode() {
  if (server.hasArg("mode")) {
    ledManualMode = (server.arg("mode") != "auto");
    Serial.printf("[API] LED Mode: %s\n", ledManualMode ? "Manual" : "Auto");
  }
  server.send(200, "text/plain", "OK");
}

void handleApiSensorMode() {
  if (server.hasArg("val")) {
    sensorEnabled = (server.arg("val").toInt() == 1);
    preferences.putBool("sensor", sensorEnabled);
    Serial.printf("[API] Sensor Mode: %s\n", sensorEnabled ? "ON" : "OFF");
  }
  server.send(200, "text/plain", "OK");
}

void handleApiDistance() {
  JsonDocument doc;
  doc["distance"] = currentDistance;
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

void setup() {
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
  }
  loadHistoryFromFile();

  preferences.begin("job", false);
  holdTimeSec     = preferences.getFloat("hold",    0.5f);
  reachTimeSec    = preferences.getFloat("reach",   0.5f);
  pin13State      = preferences.getInt("pin13",     0);
  activeLedCount  = preferences.getInt("led_cnt",   35);
  sensorEnabled   = preferences.getBool("sensor",   false);
  sensorThreshold = preferences.getFloat("sth",     10.0f);
  targetStrength  = preferences.getInt("str",       90);
  targetCount     = preferences.getInt("cnt",       3);
  for (int i = 0; i < NUM_SERVOS; i++) {
    servoOffsets[i] = preferences.getInt(PREF_OFFSET_KEYS[i],    0);
    minAngles[i]    = preferences.getInt(PREF_MIN_ANGLE_KEYS[i], 0);
    maxAngles[i]    = preferences.getInt(PREF_MAX_ANGLE_KEYS[i], 270);
  }

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN,  OUTPUT);

  pixels.begin();
  pixels.setBrightness(50);
  pixels.show();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  for (int i = 0; i < NUM_SERVOS; i++) {
    servos[i].setPeriodHertz(50);
    Serial.printf("Servo%d attached: %d\n", i + 1,
                  servos[i].attach(SERVO_PINS[i], US_AT_0_DEG, US_AT_270_DEG));
  }
  setAllServosAngle(270);

  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("焼きそば四郎");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/",                     handleRoot);
  server.on("/api/status",           handleApiStatus);
  server.on("/api/start",            handleApiStart);
  server.on("/api/stop",             handleApiStop);
  server.on("/api/settings",         handleApiSettings);
  server.on("/api/manual",           handleApiManual);
  server.on("/api/manual_angle",     handleApiManualAngle);
  server.on("/api/servo_all",        handleApiServoAll);
  server.on("/api/servo_individual", handleApiServoIndividual);
  server.on("/api/history",          handleApiHistory);
  server.on("/api/servo_offset",     handleApiServoOffset);
  server.on("/api/servo_limit",      handleApiServoLimit);
  server.on("/api/sensor_mode",      handleApiSensorMode);
  server.on("/api/distance",         handleApiDistance);
  server.on("/api/led",              handleApiLed);
  server.on("/api/led_mode",         handleApiLedMode);
  server.onNotFound([]() { handleRoot(); });

  server.begin();
  Serial.println("Ready.");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  unsigned long now = millis();

  if (now - lastDistanceMeasure > 200) {
    currentDistance     = measureDistance();
    lastDistanceMeasure = now;

    if (currentState == IDLE) {
      if (sensorEnabled && currentDistance < sensorThreshold && currentDistance > 0.1f) {
        sensorTriggerCount++;
        Serial.printf("Sensor Detect: %.1f cm (Count: %d)\n", currentDistance, sensorTriggerCount);

        if (sensorTriggerCount >= 3) {
          Serial.println(">>> Sensor START Triggered");
          sensorTriggerCount   = 0;
          currentCycle         = 0;
          sessionStartTime     = millis();
          currentState         = PREPARE_SQUEEZE;
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

  static int      lastLedState      = -1;
  static bool     lastLedManualMode = false;
  static uint32_t lastLedManualColor = 0;

  bool needLedUpdate =
      (ledManualMode != lastLedManualMode) ||
      (!ledManualMode && (int)currentState != lastLedState) ||
      (ledManualMode  && lastLedManualColor != currentLedColor) ||
      (currentState   != IDLE && !ledManualMode);

  if (needLedUpdate) {
    if (ledManualMode) {
      updateLed(currentLedColor);
      lastLedManualColor = currentLedColor;
    } else {
      switch (currentState) {
        case IDLE:
          updateLed(0);
          break;

        case FINISHED_BLINK: {

          unsigned long elapsed = millis() - stateStartTime;
          bool on = (elapsed < 200) ||
                    (elapsed >= 400 && elapsed < 600) ||
                    (elapsed >= 800 && elapsed < 1000);
          updateLed(on ? pixels.Color(0, 255, 0) : 0);
          break;
        }

        case WAIT_CYCLE:
          updateLed(0);
          break;

        case RELEASING:
          if (currentCycle >= targetCount) {

            for (int i = 0; i < LED_COUNT; i++) {
              pixels.setPixelColor(i, i < activeLedCount ? pixels.Color(0, 255, 0) : 0);
            }
            pixels.show();
          } else {

            float cycleDur = reachTimeSec + holdTimeSec + 0.3f;
            float totalDur = 0.3f + ((float)targetCount * cycleDur);
            float progress = constrain((float)(millis() - sessionStartTime) / (totalDur * 1000.0f), 0.0f, 1.0f);
            int   ledCount = (int)(progress * activeLedCount);
            for (int i = 0; i < LED_COUNT; i++) {
              pixels.setPixelColor(i, (i < activeLedCount && i < ledCount) ? pixels.Color(0, 255, 0) : 0);
            }
            pixels.show();
          }
          break;

        default: {

          float cycleDur = reachTimeSec + holdTimeSec + 0.3f;
          float totalDur = 0.3f + ((float)targetCount * cycleDur);
          float progress = constrain((float)(millis() - sessionStartTime) / (totalDur * 1000.0f), 0.0f, 1.0f);
          int   ledCount = (int)(progress * activeLedCount);
          for (int i = 0; i < LED_COUNT; i++) {
            pixels.setPixelColor(i, (i < activeLedCount && i < ledCount) ? pixels.Color(0, 255, 0) : 0);
          }
          pixels.show();
          break;
        }
      }
    }
    lastLedState      = (int)currentState;
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
      unsigned long duration = (unsigned long)(reachTimeSec * 1000);
      unsigned long elapsed  = now - stateStartTime;

      int currentTargetStr = targetStrength;
      if (gradationEnabled) {
        float progress = (targetCount > 1)
            ? (float)currentCycle / (float)(targetCount - 1)
            : 1.0f;
        currentTargetStr = gradationStart + (int)((gradationEnd - gradationStart) * progress);
      }
      int targetAngle = strengthToAngle(currentTargetStr);

      if (elapsed >= duration) {
        setAllServosAngle(targetAngle);
        currentState = HOLDING;
      } else {
        float progress   = (float)elapsed / (float)duration;
        int currentAngle = 270 + (int)((targetAngle - 270) * progress);
        setAllServosAngle(currentAngle);
      }
      yield();
      break;
    }

    case HOLDING:
      if (now - stateStartTime >= (unsigned long)(holdTimeSec * 1000)) {
        currentState = RELEASING;
        currentCycle++;
      }
      yield();
      break;

    case RELEASING:
      setAllServosAngle(270);

      if (now - stateStartTime >= 500) {
        if (currentCycle >= targetCount) {
          Serial.println("Finished.");
          currentState = FINISHED_BLINK;

          HistoryItem newItem;
          newItem.timeStr  = String(millis() / 1000) + "秒前";
          newItem.preset   = currentSessionPreset;
          newItem.strength = targetStrength;
          newItem.count    = targetCount;
          historyLog.push_back(newItem);
          if (historyLog.size() > 20) {
            historyLog.erase(historyLog.begin());
          }
          saveHistoryToFile();
        } else {
          currentState = WAIT_CYCLE;
        }
      }
      yield();
      break;

    case FINISHED_BLINK:
      if (now - stateStartTime >= 1000) {
        currentState = IDLE;
        setAllServosAngle(270);
      }
      yield();
      break;

    case WAIT_CYCLE:
      currentState = SQUEEZING;
      yield();
      break;
  }
}
