// ============================================================
//  ESP32 Servo MQTT Controller
//  ------------------------------------------------------------
//  Hardware  : ESP32 + PCA9685 (I2C @ 0x40) + 1..4 SG90
//  Software  : Wi-Fi + MQTT (PubSubClient) + Adafruit PWM + NVS
//
//  Edita parametros NO sensibles en `config.h` y credenciales
//  Wi-Fi/MQTT en `secrets.h` (copialo desde secrets.h.example).
// ============================================================
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_PWMServoDriver.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include "config.h"

// ---------- Objetos globales ----------
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PCA9685_ADDR);
WiFiClient   net;
PubSubClient mqtt(net);
Preferences  prefs;

// Estado en RAM (dimensionado a MAX_SERVOS; se usan los primeros NUM_SERVOS)
int currentAngle[MAX_SERVOS];
int presets[MAX_SERVOS][2];

unsigned long lastHeartbeat = 0;

// ============================================================
//  Utilidades
// ============================================================
int angleToTick(int angle) {
  angle = constrain(angle, 0, 180);
  return map(angle, 0, 180, SERVO_MIN_TICK, SERVO_MAX_TICK);
}

void moveServo(uint8_t idx, int angle) {
  if (idx >= NUM_SERVOS) return;
  angle = constrain(angle, 0, 180);
  pwm.setPWM(SERVO_CHANNELS[idx], 0, angleToTick(angle));
  currentAngle[idx] = angle;
}

void loadPresetsFromNVS() {
  prefs.begin("servos", true); // read-only
  for (int s = 0; s < NUM_SERVOS; s++) {
    char k0[16], k1[16];
    snprintf(k0, sizeof(k0), "s%d_p0", s);
    snprintf(k1, sizeof(k1), "s%d_p1", s);
    presets[s][0] = prefs.getInt(k0, SERVO_BOOT_ANGLE);
    presets[s][1] = prefs.getInt(k1, SERVO_BOOT_ANGLE);
  }
  prefs.end();
}

void savePresetToNVS(uint8_t servo, uint8_t slot, int angle) {
  if (servo >= NUM_SERVOS || slot >= 2) return;
  prefs.begin("servos", false); // r/w
  char key[16];
  snprintf(key, sizeof(key), "s%d_p%d", servo, slot);
  prefs.putInt(key, angle);
  prefs.end();
  presets[servo][slot] = angle;
}

// ============================================================
//  Publicar estado
// ============================================================
void publishState() {
  StaticJsonDocument<512> doc;
  doc["device_id"]  = DEVICE_ID;
  doc["num_servos"] = NUM_SERVOS;
  doc["rssi"]       = WiFi.RSSI();
  doc["uptime_s"]   = millis() / 1000;

  JsonArray channels = doc.createNestedArray("channels");
  for (int i = 0; i < NUM_SERVOS; i++) channels.add(SERVO_CHANNELS[i]);

  JsonArray angles = doc.createNestedArray("angles");
  for (int i = 0; i < NUM_SERVOS; i++) angles.add(currentAngle[i]);

  JsonArray pres = doc.createNestedArray("presets");
  for (int s = 0; s < NUM_SERVOS; s++) {
    JsonArray row = pres.createNestedArray();
    row.add(presets[s][0]);
    row.add(presets[s][1]);
  }

  char buf[512];
  size_t n = serializeJson(doc, buf);
  mqtt.publish(TOPIC_STATE, (uint8_t*)buf, n, true /* retained */);
}

// ============================================================
//  Handler de comandos MQTT
//  Formato JSON esperado en topic .../cmd :
//    { "action": "move",   "servo": 0, "angle": 90 }
//    { "action": "move_all", "angles": [10, 20, ...] }
//    { "action": "save_preset", "servo": 0, "slot": 0, "angle": 45 }
//    { "action": "load_preset", "servo": 0, "slot": 1 }
//    { "action": "load_all_preset", "slot": 0 }
//    { "action": "request_state" }
// ============================================================
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<384> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[MQTT] JSON invalido: %s\n", err.c_str());
    return;
  }
  const char* action = doc["action"] | "";

  if (!strcmp(action, "move")) {
    int s = doc["servo"] | -1;
    int a = doc["angle"] | -1;
    if (s >= 0 && s < NUM_SERVOS && a >= 0) moveServo(s, a);
  }
  else if (!strcmp(action, "move_all")) {
    JsonArray a = doc["angles"].as<JsonArray>();
    int i = 0;
    for (JsonVariant v : a) {
      if (i >= NUM_SERVOS) break;
      moveServo(i++, v.as<int>());
    }
  }
  else if (!strcmp(action, "save_preset")) {
    int s = doc["servo"] | -1;
    int slot = doc["slot"]  | -1;
    int a = doc["angle"]    | currentAngle[ s >= 0 ? s : 0 ];
    if (s >= 0 && s < NUM_SERVOS && slot >= 0 && slot < 2)
      savePresetToNVS(s, slot, a);
  }
  else if (!strcmp(action, "load_preset")) {
    int s = doc["servo"] | -1;
    int slot = doc["slot"]  | -1;
    if (s >= 0 && s < NUM_SERVOS && slot >= 0 && slot < 2)
      moveServo(s, presets[s][slot]);
  }
  else if (!strcmp(action, "load_all_preset")) {
    int slot = doc["slot"] | -1;
    if (slot >= 0 && slot < 2)
      for (int s = 0; s < NUM_SERVOS; s++) moveServo(s, presets[s][slot]);
  }
  else if (!strcmp(action, "request_state")) {
    // se envia abajo
  }
  else {
    Serial.printf("[MQTT] action desconocida: %s\n", action);
    return;
  }
  publishState();
}

// ============================================================
//  Conexion Wi-Fi / MQTT
// ============================================================
void connectWifi() {
  Serial.printf("[WiFi] Conectando a \"%s\" ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
    if (millis() - t0 > 20000) { Serial.println(" timeout, retry"); WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASSWORD); t0 = millis(); }
  }
  Serial.printf("\n[WiFi] OK, IP=%s, RSSI=%d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
}

void connectMqtt() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqtt.setBufferSize(1024);

  while (!mqtt.connected()) {
    String clientId = String("esp32-") + DEVICE_ID + "-" + String((uint32_t)esp_random(), HEX);
    Serial.printf("[MQTT] Conectando a %s:%d como %s ... ", MQTT_HOST, MQTT_PORT, clientId.c_str());

    bool ok;
    if (strlen(MQTT_USER) > 0) {
      ok = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                        TOPIC_LWT, 1, true, "offline");
    } else {
      ok = mqtt.connect(clientId.c_str(), nullptr, nullptr,
                        TOPIC_LWT, 1, true, "offline");
    }

    if (ok) {
      Serial.println("OK");
      mqtt.publish(TOPIC_LWT, "online", true);
      mqtt.subscribe(TOPIC_CMD, 1);
      publishState();
    } else {
      Serial.printf("fallo rc=%d, reintento en 2s\n", mqtt.state());
      delay(2000);
    }
  }
}

// ============================================================
//  Setup / Loop
// ============================================================
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println("\n=== ESP32 Servo MQTT Controller ===");
  Serial.printf("DEVICE_ID = %s\n", DEVICE_ID);
  Serial.printf("NUM_SERVOS = %d\n", NUM_SERVOS);

  Wire.begin(I2C_SDA, I2C_SCL);
  pwm.begin();
  pwm.setPWMFreq(PWM_FREQ);
  delay(10);

  // Inicializa estado RAM con la posicion segura.
  for (int i = 0; i < MAX_SERVOS; i++) currentAngle[i] = SERVO_BOOT_ANGLE;

  loadPresetsFromNVS();

  // Posicion inicial segura para los servos activos
  for (int i = 0; i < NUM_SERVOS; i++) moveServo(i, SERVO_BOOT_ANGLE);

  connectWifi();
  connectMqtt();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWifi();
  if (!mqtt.connected())             connectMqtt();
  mqtt.loop();

  if (millis() - lastHeartbeat > HEARTBEAT_MS) {
    lastHeartbeat = millis();
    publishState();
  }
}
