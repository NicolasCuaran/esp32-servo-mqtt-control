# Arquitectura

```
+--------------------------+         +--------------------------+
|        Navegador         |   WS    |       Backend (FastAPI)  |
|  index.html / app.js     | ------> |  main.py                 |
|  (sliders, gauges)       | <------ |  - REST  /api/*          |
+----------+---------------+  REST   |  - WS    /ws             |
           |                          |  - paho-mqtt client      |
           |                          +------+-------------------+
           |                                 | MQTT (QoS 1)
           |                                 v
           |                          +--------------+
           |                          |  broker.emqx |  (publico)
           |                          +------+-------+
           |                                 |
           |                                 v
           |                          +--------------------------+
           |                          |           ESP32          |
           |                          |  WiFi + PubSubClient     |
           |                          |  Adafruit PWMServoDriver |
           |                          |  Preferences (NVS)       |
           |                          +------+-------------------+
           |                                 | I2C
           |                          +------v-----------+
           |                          |      PCA9685     |
           |                          +------+-----------+
           |                                 |
           v                                 v
        (la web pinta tantas tarjetas      Servo 1 (canal 0)
         como servos reporte el ESP32)     Servo 2 (canal 7)   [opcional]
                                           Servo 3 (canal 8)   [opcional]
                                           Servo 4 (canal 15)  [opcional]
```

## Flujo de un movimiento

1. El usuario arrastra el slider del Servo 1 en la web.
2. JS hace `POST /api/move {servo:0, angle:120}`.
3. FastAPI publica en MQTT: `topic="servos/<id>/cmd"`,
   payload `{"action":"move","servo":0,"angle":120}`.
4. El ESP32 (suscrito a ese topic) recibe y llama
   `pwm.setPWM(SERVO_CHANNELS[0], 0, angleToTick(120))`.
5. Tras aplicar el cambio, el ESP32 publica su nuevo estado en
   `servos/<id>/state`.
6. El backend recibe ese mensaje y lo difunde por WebSocket a todos los
   navegadores conectados, que actualizan sus gauges.

## NUM_SERVOS configurable

- `firmware/.../config.h` define `NUM_SERVOS` (1..4) y `SERVO_CHANNELS[]`.
- `server/.env` define `NUM_SERVOS` (sugerido).
- El ESP32 incluye `num_servos` y `channels` en cada `state`. Si difiere de
  lo que dice el `.env`, el backend usa la cifra del ESP32 y la UI se
  re-renderiza automaticamente para reflejar la cantidad real.

## Por que MQTT (y no HTTP directo al ESP32)

- **Reconexion transparente**: el ESP32 puede perder Wi-Fi y reconectar sin
  que el servidor tenga que descubrirlo de nuevo.
- **N a N**: varios navegadores pueden controlar el mismo ESP32 y verse entre
  si en tiempo real sin logica extra en el firmware.
- **Last Will**: si el ESP32 muere, el broker avisa automaticamente
  (`offline` retenido) y la UI lo refleja en milisegundos.
- **Bajo overhead**: payloads JSON < 256 B, conexion persistente con keep-alive.

## Persistencia de presets

El ESP32 guarda `NUM_SERVOS * 2` enteros en la NVS (namespace `servos`,
claves `s{servo}_p{slot}`). Sobreviven a corte de luz y a reset. Al arrancar,
se cargan en RAM y se publican en el primer `state` enviado al servidor.

## Aislamiento de credenciales

`secrets.h` (ignorado por git) contiene `WIFI_SSID`, `WIFI_PASSWORD`,
`MQTT_USER` y `MQTT_PASS`. `config.h` lo incluye y expone los identificadores
al sketch. Esto permite versionar `config.h` libremente sin filtrar
credenciales y mantener `secrets.h.example` como plantilla para nuevos clones.
