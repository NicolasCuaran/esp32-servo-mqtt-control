# Firmware ESP32 - `esp32_servo_controller`

Sketch Arduino que conecta el ESP32 a Wi-Fi, se suscribe a un broker MQTT
y mueve **N servos SG90 (N = 1..4)** conectados a un modulo PCA9685
(I2C @ `0x40`).

## Conexionado

| ESP32   | PCA9685 | Notas                       |
|---------|---------|-----------------------------|
| GPIO 21 | SDA     | I2C SDA                     |
| GPIO 22 | SCL     | I2C SCL                     |
| 3V3     | VCC     | Logica                      |
| GND     | GND     | Comun                       |
| -       | V+      | **5-6 V externos** (servos) |

Configuracion por defecto: **1 servo** conectado al canal **0** del PCA9685.

Para usar mas servos: edita `NUM_SERVOS` y `SERVO_CHANNELS` en `config.h`
(p. ej. `NUM_SERVOS=4`, `SERVO_CHANNELS = {0, 7, 8, 15}`).

> Alimenta los servos con una fuente externa de 5 V (>= 1 A por SG90).
> Une los GND del ESP32, PCA9685 y fuente externa.

## Librerias requeridas (Arduino IDE - *Library Manager*)

- `Adafruit PWM Servo Driver Library`
- `PubSubClient` (Nick O'Leary)
- `ArduinoJson` (>= 7.x)

Board package: **esp32 by Espressif** (instala desde el *Boards Manager*
agregando `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
en *Preferences*).

## Configuracion

Antes de compilar, **copia las credenciales**:

```bash
cd firmware/esp32_servo_controller
cp secrets.h.example secrets.h
# Edita secrets.h con tu SSID y password reales
```

- [`secrets.h`](./secrets.h.example): credenciales Wi-Fi y (opcional) MQTT.
  Esta en `.gitignore`, NO se sube al repo.
- [`config.h`](./config.h): parametros no sensibles (broker MQTT, pines,
  `DEVICE_ID`, `NUM_SERVOS`, calibracion del servo).

## Topics MQTT

| Topic                          | Direccion | Carga util (JSON)                                     |
|--------------------------------|-----------|-------------------------------------------------------|
| `servos/<id>/cmd`              | server->esp| `{"action":"move","servo":0,"angle":90}`              |
| `servos/<id>/state`            | esp->server| `{"num_servos":1, "channels":[0], "angles":[...], "presets":[[...]], "rssi":-55,...}` |
| `servos/<id>/online` (LWT)     | esp->broker| `online` / `offline` (retained)                       |

### Acciones soportadas

```jsonc
{"action":"move",            "servo":0, "angle":90}
{"action":"move_all",        "angles":[10, 20]}
{"action":"save_preset",     "servo":0, "slot":0, "angle":45}
{"action":"load_preset",     "servo":0, "slot":1}
{"action":"load_all_preset", "slot":0}
{"action":"request_state"}
```

Los presets se guardan en **NVS** (memoria flash del ESP32) - sobreviven al reinicio.

## Compilar / subir

1. Conecta el ESP32 por USB.
2. Abre `esp32_servo_controller.ino` en Arduino IDE.
3. *Tools - Board -* `ESP32 Dev Module` (o tu placa).
4. *Tools - Port -* tu puerto `COMx`.
5. Pulsa **Upload**.
6. Abre el *Serial Monitor* a 115200 baud para ver los logs.
