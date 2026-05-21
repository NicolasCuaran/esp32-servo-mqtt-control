# Servidor Python (FastAPI + MQTT)

Bridge entre el ESP32 (via MQTT) y la interfaz web (via HTTP + WebSocket).

## Instalacion

Requiere **Python 3.10+**.

```bash
cd server
python -m venv .venv
source .venv/bin/activate            # En Windows: .venv\Scripts\activate
pip install -r requirements.txt
cp .env.example .env                 # Ajusta DEVICE_ID y NUM_SERVOS si los cambiaste
```

## Ejecutar

```bash
python main.py
# o, equivalente:
uvicorn main:app --host 0.0.0.0 --port 8000
```

Luego abre **http://localhost:8000** en el navegador.

## Endpoints REST

| Metodo | Ruta                          | Body                                            |
|--------|-------------------------------|-------------------------------------------------|
| GET    | `/api/config`                 | -  (num_servos, channels, device_id)            |
| GET    | `/api/status`                 | -                                               |
| POST   | `/api/move`                   | `{ "servo": 0..N-1, "angle": 0..180 }`          |
| POST   | `/api/move_all`               | `{ "angles": [a, b, ...] }` (1..N)              |
| POST   | `/api/preset/save`            | `{ "servo": 0..N-1, "slot": 0..1, "angle": 0..180 }` |
| POST   | `/api/preset/load`            | `{ "servo": 0..N-1, "slot": 0..1 }`             |
| POST   | `/api/preset/load_all`        | `{ "slot": 0..1 }`                              |
| POST   | `/api/request_state`          | -                                               |
| WS     | `/ws`                         | recibe el JSON de estado en vivo                |

OpenAPI interactivo en **/docs**.

## Variables de entorno

Ver [`.env.example`](./.env.example).

> `DEVICE_ID` y `NUM_SERVOS` deben coincidir con los del firmware
> (`firmware/.../config.h`). Si el ESP32 reporta otra cantidad de servos,
> el servidor la respeta automaticamente.
