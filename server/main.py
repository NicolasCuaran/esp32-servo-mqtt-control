"""
ESP32 Servo MQTT Control - backend FastAPI

Funciones:
- Bridge MQTT <-> HTTP/WebSocket entre el ESP32 y la web.
- API REST para mover servos y gestionar presets.
- WebSocket /ws para empujar el estado en tiempo real al navegador.
- Sirve la web estatica (carpeta /web).

Configuracion en .env (ver .env.example).
"""
from __future__ import annotations

import asyncio
import json
import logging
import os
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, field_validator

# ---------- Config ----------
load_dotenv()
MQTT_HOST  = os.getenv("MQTT_HOST", "broker.emqx.io")
MQTT_PORT  = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER  = os.getenv("MQTT_USER", "")
MQTT_PASS  = os.getenv("MQTT_PASS", "")
DEVICE_ID  = os.getenv("DEVICE_ID", "ece88b61")
NUM_SERVOS = max(1, min(4, int(os.getenv("NUM_SERVOS", "1"))))

TOPIC_CMD   = f"servos/{DEVICE_ID}/cmd"
TOPIC_STATE = f"servos/{DEVICE_ID}/state"
TOPIC_LWT   = f"servos/{DEVICE_ID}/online"

WEB_DIR = (Path(__file__).resolve().parent.parent / "web").resolve()

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)-7s | %(name)s | %(message)s",
)
log = logging.getLogger("servo-server")


# ---------- Estado compartido ----------
class State:
    """Ultima snapshot conocida del ESP32, accesible a los websockets."""

    def __init__(self) -> None:
        self.online: bool = False
        # num_servos: lo que dice el .env, se sobreescribe con lo que reporte el ESP32
        self.num_servos: int = NUM_SERVOS
        self.last_state: dict[str, Any] = {
            "device_id":  DEVICE_ID,
            "num_servos": NUM_SERVOS,
            "channels":   list(range(NUM_SERVOS)),
            "angles":     [90] * NUM_SERVOS,
            "presets":    [[90, 90]] * NUM_SERVOS,
            "rssi":       None,
            "uptime_s":   0,
            "ts":         0,
        }
        self.clients: set[WebSocket] = set()
        self.loop: asyncio.AbstractEventLoop | None = None

    def snapshot(self) -> dict[str, Any]:
        return {"online": self.online, **self.last_state}


state = State()


# ---------- MQTT ----------
def on_connect(client: mqtt.Client, _u, _f, rc, _props=None):
    if rc == 0:
        log.info("MQTT conectado a %s:%s", MQTT_HOST, MQTT_PORT)
        client.subscribe(TOPIC_STATE, qos=1)
        client.subscribe(TOPIC_LWT, qos=1)
    else:
        log.error("MQTT connect rc=%s", rc)


def on_message(_c, _u, msg: mqtt.MQTTMessage):
    if msg.topic == TOPIC_LWT:
        state.online = msg.payload.decode(errors="ignore") == "online"
        log.info("ESP32 -> %s", "ONLINE" if state.online else "OFFLINE")
        _broadcast_threadsafe(state.snapshot())
        return

    if msg.topic == TOPIC_STATE:
        try:
            data = json.loads(msg.payload.decode())
        except Exception as e:
            log.warning("state JSON invalido: %s", e)
            return
        data["ts"] = int(time.time())
        # Si el ESP32 reporta una cantidad distinta de servos, prevalece la suya
        if isinstance(data.get("num_servos"), int) and 1 <= data["num_servos"] <= 4:
            state.num_servos = data["num_servos"]
        state.last_state = data
        state.online = True
        _broadcast_threadsafe(state.snapshot())


def _broadcast_threadsafe(payload: dict[str, Any]) -> None:
    """Programa el broadcast desde el thread de paho al event loop de FastAPI."""
    if state.loop is None:
        return
    asyncio.run_coroutine_threadsafe(_broadcast(payload), state.loop)


async def _broadcast(payload: dict[str, Any]) -> None:
    if not state.clients:
        return
    msg = json.dumps(payload)
    dead: list[WebSocket] = []
    for ws in state.clients:
        try:
            await ws.send_text(msg)
        except Exception:
            dead.append(ws)
    for ws in dead:
        state.clients.discard(ws)


# Construccion del cliente MQTT con compatibilidad paho 2.x
try:
    mqtt_client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"servo-server-{DEVICE_ID}",
    )
except AttributeError:                              # paho < 2.0
    mqtt_client = mqtt.Client(client_id=f"servo-server-{DEVICE_ID}")
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
if MQTT_USER:
    mqtt_client.username_pw_set(MQTT_USER, MQTT_PASS)


def publish_cmd(payload: dict[str, Any]) -> None:
    """Publica un comando al ESP32."""
    raw = json.dumps(payload, separators=(",", ":"))
    log.info("CMD -> %s", raw)
    mqtt_client.publish(TOPIC_CMD, raw, qos=1)


def _max_servo_idx() -> int:
    return state.num_servos - 1


# ---------- FastAPI ----------
@asynccontextmanager
async def lifespan(_app: FastAPI):
    state.loop = asyncio.get_running_loop()
    mqtt_client.connect_async(MQTT_HOST, MQTT_PORT, keepalive=30)
    mqtt_client.loop_start()
    log.info("API lista. WEB_DIR=%s, NUM_SERVOS=%d", WEB_DIR, NUM_SERVOS)
    try:
        yield
    finally:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()


app = FastAPI(
    title="ESP32 Servo MQTT Control",
    version="1.1.0",
    description="Puente HTTP/WebSocket <-> MQTT para controlar servos en un ESP32+PCA9685.",
    lifespan=lifespan,
)


# ---------- Modelos ----------
class MoveBody(BaseModel):
    servo: int = Field(..., ge=0, le=3)
    angle: int = Field(..., ge=0, le=180)


class MoveAllBody(BaseModel):
    angles: list[int] = Field(..., min_length=1, max_length=4)

    @field_validator("angles")
    @classmethod
    def _check_range(cls, v: list[int]) -> list[int]:
        for a in v:
            if not 0 <= a <= 180:
                raise ValueError("angle fuera de rango (0..180)")
        return v


class SavePresetBody(BaseModel):
    servo: int = Field(..., ge=0, le=3)
    slot:  int = Field(..., ge=0, le=1)
    angle: int = Field(..., ge=0, le=180)


class LoadPresetBody(BaseModel):
    servo: int = Field(..., ge=0, le=3)
    slot:  int = Field(..., ge=0, le=1)


class LoadAllPresetBody(BaseModel):
    slot: int = Field(..., ge=0, le=1)


def _check_servo_index(idx: int) -> None:
    if idx > _max_servo_idx():
        raise HTTPException(400, f"servo {idx} fuera de rango (max {_max_servo_idx()})")


# ---------- Endpoints ----------
@app.get("/api/config")
def api_config() -> dict[str, Any]:
    """Devuelve la cantidad de servos activos para que la UI se autoconfigure."""
    return {
        "device_id":  DEVICE_ID,
        "num_servos": state.num_servos,
        "channels":   state.last_state.get("channels") or list(range(state.num_servos)),
    }


@app.get("/api/status")
def status() -> dict[str, Any]:
    return state.snapshot()


@app.post("/api/move")
def api_move(body: MoveBody):
    _check_servo_index(body.servo)
    publish_cmd({"action": "move", "servo": body.servo, "angle": body.angle})
    return {"ok": True}


@app.post("/api/move_all")
def api_move_all(body: MoveAllBody):
    angles = body.angles[: state.num_servos]
    publish_cmd({"action": "move_all", "angles": angles})
    return {"ok": True}


@app.post("/api/preset/save")
def api_save_preset(body: SavePresetBody):
    _check_servo_index(body.servo)
    publish_cmd({
        "action": "save_preset",
        "servo": body.servo, "slot": body.slot, "angle": body.angle,
    })
    return {"ok": True}


@app.post("/api/preset/load")
def api_load_preset(body: LoadPresetBody):
    _check_servo_index(body.servo)
    publish_cmd({"action": "load_preset", "servo": body.servo, "slot": body.slot})
    return {"ok": True}


@app.post("/api/preset/load_all")
def api_load_all(body: LoadAllPresetBody):
    publish_cmd({"action": "load_all_preset", "slot": body.slot})
    return {"ok": True}


@app.post("/api/request_state")
def api_request_state():
    publish_cmd({"action": "request_state"})
    return {"ok": True}


# ---------- WebSocket en vivo ----------
@app.websocket("/ws")
async def ws_state(ws: WebSocket):
    await ws.accept()
    state.clients.add(ws)
    log.info("WS conectado (%d clientes)", len(state.clients))
    try:
        await ws.send_text(json.dumps(state.snapshot()))
        while True:
            await ws.receive_text()       # keep-alive (ignoramos el contenido)
    except WebSocketDisconnect:
        pass
    finally:
        state.clients.discard(ws)
        log.info("WS desconectado (%d clientes)", len(state.clients))


# ---------- Estaticos / index ----------
if WEB_DIR.exists():
    app.mount("/static", StaticFiles(directory=WEB_DIR), name="static")

    @app.get("/")
    def index():
        return FileResponse(WEB_DIR / "index.html")

    @app.get("/{path:path}")
    def assets(path: str):
        target = (WEB_DIR / path).resolve()
        if not str(target).startswith(str(WEB_DIR)) or not target.is_file():
            raise HTTPException(404)
        return FileResponse(target)


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(
        "main:app",
        host=os.getenv("API_HOST", "0.0.0.0"),
        port=int(os.getenv("API_PORT", "8000")),
        reload=False,
    )
