<div align="center">

# ESP32 Servo MQTT Control

**Control en tiempo real de servos SG90 (PCA9685) desde una interfaz web,
con un ESP32 como puente Wi-Fi/MQTT y un backend FastAPI.**

[![ESP32](https://img.shields.io/badge/Hardware-ESP32-black?logo=espressif&logoColor=white)](https://www.espressif.com/)
[![PCA9685](https://img.shields.io/badge/Driver-PCA9685-2b6cb0)](https://www.adafruit.com/product/815)
[![FastAPI](https://img.shields.io/badge/Backend-FastAPI-009688?logo=fastapi&logoColor=white)](https://fastapi.tiangolo.com/)
[![MQTT](https://img.shields.io/badge/Protocol-MQTT-660066?logo=mqtt)](https://mqtt.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

</div>

---

## Caracteristicas

- **Firmware ESP32 (Arduino)** que conecta el chip al Wi-Fi y a un broker MQTT.
- **Driver PCA9685** moviendo **N servos SG90 (N = 1..4)** configurables.
  Default: **1 servo en el canal 0**.
- **Backend FastAPI** que actua de puente MQTT <-> HTTP/WebSocket.
- **Interfaz web** con sliders, gauges SVG y feedback en vivo (WebSocket).
  Se autoconfigura segun lo que reporte el ESP32 (`/api/config`).
- **2 posiciones predefinidas por servo** guardadas en la **NVS** del ESP32
  (sobreviven al reinicio).
- Reconexion automatica Wi-Fi y MQTT, *Last Will* `online/offline`.
- **Credenciales aisladas** en `secrets.h` (no versionado).
- Diseno dark, responsive, sin frameworks pesados.

---

## Arquitectura

```
   +---------+      Wi-Fi      +--------------+    REST     +------------+
   |  ESP32  | <---- MQTT ---> |  FastAPI app | <--------> |  Navegador |
   |+PCA9685 |   broker.emqx   |  + paho-mqtt |   /ws WS    |   (UI)     |
   +----+----+                 +--------------+             +------------+
        | I2C
   +----v----+
   | PCA9685 |-- canal 0 --> Servo 1 (SG90)   [default: 1 servo]
   |         |-- canal 7 --> Servo 2 (SG90)   [opcional]
   |         |-- canal 8 --> Servo 3 (SG90)   [opcional]
   |         |-- canal 15-> Servo 4 (SG90)   [opcional]
   +---------+
```

| Capa     | Tecnologia                                                      |
|----------|-----------------------------------------------------------------|
| Firmware | Arduino C++, PubSubClient, Adafruit PWM, ArduinoJson, NVS       |
| Bridge   | Python 3.10+, FastAPI, paho-mqtt, WebSockets                    |
| UI       | HTML5, CSS3 puro, JS vanilla (sin build step)                   |
| Broker   | [broker.emqx.io](https://www.emqx.com/en/mqtt/public-mqtt5-broker) (publico, gratuito) - facil de cambiar |

---

## Hardware

| Pieza               | Detalles                                       |
|---------------------|------------------------------------------------|
| MCU                 | ESP32 DevKit V1 (cualquier variante con I2C)   |
| Driver PWM          | PCA9685 16-canal (I2C, direccion `0x40`)       |
| Servos              | 1..4 TzT MicroServo 9g SG90                    |
| Alimentacion servos | Fuente externa 5 V (>=1 A por SG90)            |

### Conexionado

| ESP32   | PCA9685 |
|---------|---------|
| 3V3     | VCC     |
| GND     | GND     |
| GPIO 21 | SDA     |
| GPIO 22 | SCL     |
| **GND** | **GND fuente externa** |

PCA9685 -> Servo en canal **0** (por defecto). Para usar mas servos, cambia
`NUM_SERVOS` en `config.h` y conecta en los canales declarados en
`SERVO_CHANNELS` (default `{0, 7, 8, 15}`).
**V+** del PCA9685 conectado a la **fuente externa de 5 V**.

> **No alimentes los servos desde el ESP32.** Un SG90 con carga puede pedir
> picos > 600 mA y reiniciar el microcontrolador.

---

## Configuracion

### Firmware

1. **Credenciales Wi-Fi** (no versionadas) en
   [`firmware/esp32_servo_controller/secrets.h`](firmware/esp32_servo_controller/secrets.h.example):

   ```bash
   cd firmware/esp32_servo_controller
   cp secrets.h.example secrets.h
   # Edita secrets.h con tu SSID y password reales
   ```

2. **Parametros no sensibles** en
   [`firmware/esp32_servo_controller/config.h`](firmware/esp32_servo_controller/config.h):

   - `MQTT_HOST`, `MQTT_PORT`
   - `DEVICE_ID` (cambialo para no compartir topic con otros usuarios del broker publico)
   - `NUM_SERVOS` (default `1`)
   - `SERVO_CHANNELS` (default `{0, 7, 8, 15}`)
   - Pines I2C y calibracion del servo

### Servidor

Variables en [`server/.env`](server/.env.example) (copialo desde `.env.example`):

- Mismo `DEVICE_ID` que el firmware.
- Mismo `NUM_SERVOS` (si difiere, prevalece el que reporte el ESP32).
- Mismo broker.

---

## Puesta en marcha (3 pasos)

### 1) Firmware

```text
Arduino IDE -> Library Manager, instala:
  - Adafruit PWM Servo Driver Library
  - PubSubClient
  - ArduinoJson
Boards Manager -> esp32 by Espressif

cd firmware/esp32_servo_controller
cp secrets.h.example secrets.h        # edita con tus credenciales

Abre esp32_servo_controller.ino
Selecciona placa "ESP32 Dev Module" y tu puerto COM -> Upload
```

Abre el monitor serie a `115200` y deberias ver:

```
=== ESP32 Servo MQTT Controller ===
DEVICE_ID = ece88b61
NUM_SERVOS = 1
[WiFi] OK, IP=192.168.x.x
[MQTT] Conectando a broker.emqx.io:1883 ... OK
```

### 2) Backend

```bash
cd server
python -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt
cp .env.example .env               # ajusta NUM_SERVOS si lo cambiaste
python main.py
```

### 3) Interfaz

Abre **http://localhost:8000** en tu navegador. La UI se autoconfigura: pinta
tantas tarjetas como servos haya activos.

---

## Como funciona

El backend publica comandos JSON en `servos/<DEVICE_ID>/cmd` y se suscribe a
`servos/<DEVICE_ID>/state` para recibir telemetria. Cuando llega un mensaje
nuevo, lo reenvia por WebSocket a todos los navegadores conectados.

Comandos soportados (todos JSON):

```jsonc
{"action":"move",            "servo":0, "angle":90}
{"action":"move_all",        "angles":[10,20]}
{"action":"save_preset",     "servo":0, "slot":0, "angle":45}
{"action":"load_preset",     "servo":0, "slot":1}
{"action":"load_all_preset", "slot":0}
{"action":"request_state"}
```

Endpoints REST equivalentes:

| Endpoint                  | Body                                       |
|---------------------------|--------------------------------------------|
| `GET  /api/config`        | -  (num_servos, channels, device_id)       |
| `POST /api/move`          | `{servo, angle}`                           |
| `POST /api/move_all`      | `{angles:[...]}`                           |
| `POST /api/preset/save`   | `{servo, slot, angle}`                     |
| `POST /api/preset/load`   | `{servo, slot}`                            |
| `POST /api/preset/load_all` | `{slot}`                                 |
| `GET  /api/status`        | -                                          |
| `WS   /ws`                | recibe el snapshot completo en cada cambio |

Documentacion OpenAPI auto-generada en `/docs`.

---

## Estructura

```
esp32-servo-mqtt-control/
|-- firmware/
|   `-- esp32_servo_controller/
|       |-- esp32_servo_controller.ino
|       |-- config.h               <- broker, pines, NUM_SERVOS
|       |-- secrets.h.example      <- plantilla (versionada)
|       |-- secrets.h              <- credenciales reales (NO versionado)
|       `-- README.md
|-- server/
|   |-- main.py                    <- FastAPI + paho-mqtt + WebSocket
|   |-- requirements.txt
|   |-- .env.example
|   `-- README.md
|-- web/
|   |-- index.html
|   |-- css/style.css
|   `-- js/app.js
|-- docs/
|   `-- architecture.md
|-- .gitignore
|-- LICENSE  (MIT)
|-- CONTRIBUTING.md
|-- CODE_OF_CONDUCT.md
`-- README.md
```

---

## Notas de seguridad

- Las credenciales Wi-Fi viven en `secrets.h`, que esta en `.gitignore`.
  **Nunca commitees** este archivo.
- `broker.emqx.io` es **publico**: cualquiera que conozca tu `DEVICE_ID` puede
  enviar comandos. Para un despliegue serio:
  1. Cambia el `DEVICE_ID` por algo mas largo y privado.
  2. Mejor aun: monta tu propio **Mosquitto** con usuario/contrasena, o usa
     **HiveMQ Cloud** (TLS gratis hasta 100 dispositivos) y actualiza
     `MQTT_HOST/PORT/USER/PASS` en ambos lados.
  3. Coloca el servidor FastAPI tras autenticacion (basic auth, token, etc.)
     si lo expones a Internet.

---

## Contribuir

PRs bienvenidos. Lee [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Licencia

[MIT](LICENSE) - 2026 Nicolas Cuaran
