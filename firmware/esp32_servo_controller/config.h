// ============================================================
//  config.h - Configuracion del controlador de servos ESP32
// ============================================================
//  Las CREDENCIALES viven en `secrets.h` (no versionado).
//  Copia `secrets.h.example` como `secrets.h` y edita ahi tu
//  SSID y password. Aqui solo van parametros no sensibles.
// ============================================================
#ifndef CONFIG_H
#define CONFIG_H

#include "secrets.h"   // define WIFI_SSID, WIFI_PASSWORD, MQTT_USER, MQTT_PASS

// ---------- MQTT ----------
// Broker publico gratuito de EMQX (sin autenticacion).
// Puedes cambiarlo por uno propio (p. ej. mosquitto local o HiveMQ Cloud).
#define MQTT_HOST       "broker.emqx.io"
#define MQTT_PORT       1883

// Identificador unico del dispositivo. DEBE coincidir con el del
// servidor Python (server/.env -> DEVICE_ID). Cambialo si quieres
// evitar colisiones con otros usuarios del broker publico.
#define DEVICE_ID       "ece88b61"

// Topics (no es necesario tocarlos: derivan del DEVICE_ID)
#define TOPIC_CMD       "servos/" DEVICE_ID "/cmd"
#define TOPIC_STATE     "servos/" DEVICE_ID "/state"
#define TOPIC_PRESET    "servos/" DEVICE_ID "/preset"
#define TOPIC_LWT       "servos/" DEVICE_ID "/online"

// ---------- PCA9685 ----------
#define PCA9685_ADDR    0x40      // direccion I2C por defecto
#define I2C_SDA         21        // SDA del ESP32 (ajusta si lo necesitas)
#define I2C_SCL         22        // SCL del ESP32
#define PWM_FREQ        50        // Hz, estandar para servos analogicos

// Calibracion SG90 (en ticks de 12-bit del PCA9685 a 50 Hz)
// Si tus servos llegan menos de 180 grados o vibran, ajusta estos valores.
#define SERVO_MIN_TICK  125       // ~0 grados   (~0.5 ms)
#define SERVO_MAX_TICK  575       // ~180 grados (~2.5 ms)

// ---------- Cantidad y mapeo de servos ----------
// Numero de servos REALMENTE conectados. Min 1, max 4 (limite de los
// arrays estaticos definidos abajo; sube MAX_SERVOS para mas).
#define NUM_SERVOS      1
#define MAX_SERVOS      4

// Pines del PCA9685 donde estan conectados los servos (0..15).
// La cantidad de elementos usados es NUM_SERVOS; los demas se ignoran.
// Configuracion completa (4 servos): { 0, 7, 8, 15 }
const uint8_t SERVO_CHANNELS[MAX_SERVOS] = { 0, 7, 8, 15 };

// Posicion segura al arrancar (en grados)
#define SERVO_BOOT_ANGLE 90

// ---------- Otros ----------
#define SERIAL_BAUD     115200
#define HEARTBEAT_MS    5000      // publicar estado cada N ms

#endif // CONFIG_H
