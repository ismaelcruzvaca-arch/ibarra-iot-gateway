#!/usr/bin/env python3
"""
GEMA V3.0 — Industrial Machine Simulator (Sparkplug B "Lite")

Simulates 7 machines publishing to MQTT with LWT and heartbeats.
Cumple: Sparkplug B Lite (node_health + LWT + NDEATH)
Cumple: ISA-95 Nivel 0 (Sensores) → MQTT Broker
Cumple: Payload Universal con metrics: [] dinámico
"""

import json
import time
import random
import threading
import signal
import sys
from paho.mqtt import client as mqtt

# --- Configuration ---
BROKER_HOST = "mosquitto"
BROKER_PORT = 1883
HEARTBEAT_INTERVAL = 10   # seconds — minimum interval between publishes
DEBOUNCE_INTERVAL = 2     # seconds — simulated anti-bounce

MACHINES = {
    "produccion_a": {
        "prensa_01": {
            "metrics": ["temperature", "pressure", "cycles"],
            "ranges": [(60, 120), (8, 16), (0, 1000)]
        },
        "prensa_02": {
            "metrics": ["temperature", "pressure", "cycles"],
            "ranges": [(60, 120), (8, 16), (0, 1000)]
        },
        "torno_cnc": {
            "metrics": ["spindle_speed", "feed_rate", "tool_wear"],
            "ranges": [(800, 3000), (0.1, 0.5), (0, 100)]
        },
        "robot_soldador": {
            "metrics": ["weld_temp", "wire_feed", "arc_voltage"],
            "ranges": [(1500, 2000), (5, 15), (20, 40)]
        }
    },
    "produccion_b": {
        "cinta_transp": {
            "metrics": ["speed", "load_percent", "motor_current"],
            "ranges": [(0.5, 3.0), (0, 100), (5, 30)]
        },
        "horno_trat": {
            "metrics": ["chamber_temp", "conveyor_speed", "energy_kw"],
            "ranges": [(800, 1200), (0.1, 0.8), (50, 200)]
        },
        "lavadora_ind": {
            "metrics": ["drum_speed", "water_temp", "cycle_count"],
            "ranges": [(0, 1200), (20, 95), (0, 500)]
        }
    }
}

STOP_EVENT = threading.Event()


# ---------------------------------------------------------------------------
# Metric unit inference
# ---------------------------------------------------------------------------

_UNITS = {
    "temperature": "celsius",
    "pressure": "bar",
    "cycles": "count",
    "spindle_speed": "rpm",
    "feed_rate": "mm_s",
    "tool_wear": "pct",
    "weld_temp": "celsius",
    "wire_feed": "m_min",
    "arc_voltage": "volt",
    "speed": "m_s",
    "load_percent": "pct",
    "motor_current": "amp",
    "chamber_temp": "celsius",
    "conveyor_speed": "m_s",
    "energy_kw": "kw",
    "drum_speed": "rpm",
    "water_temp": "celsius",
    "cycle_count": "count",
}


def infer_unit(name: str) -> str:
    """Return the SI or common unit string for a known metric name."""
    return _UNITS.get(name, "unknown")


# ---------------------------------------------------------------------------
# Payload builder (Sparkplug B Lite)
# ---------------------------------------------------------------------------

def build_payload(area: str, machine: str, cfg: dict) -> dict:
    """Build Sparkplug B Lite universal payload with dynamic metrics array.

    Every payload contains:
      - node_health: str        (Sparkplug B Lite required field)
      - heartbeat: int          (epoch ms)
      - metrics: list[dict]     (dynamic array — one entry per metric)

    Each metric entry:
      - name: str
      - value: float
      - timestamp: int (epoch ms)
      - unit: str
    """
    metrics = []
    for i, name in enumerate(cfg["metrics"]):
        low, high = cfg["ranges"][i]
        metrics.append({
            "name": name,
            "value": round(random.uniform(low, high), 2),
            "timestamp": int(time.time() * 1000),
            "unit": infer_unit(name),
        })

    return {
        "node_health": "ONLINE",
        "heartbeat": int(time.time() * 1000),
        "metrics": metrics,
    }


# ---------------------------------------------------------------------------
# MQTT client factory (one per machine)
# ---------------------------------------------------------------------------

def create_machine_client(area: str, machine: str, cfg: dict):
    """Create and return an MQTT client for a machine with LWT.

    LWT payload is JSON with ``node_health: "OFFLINE"`` — the broker
    publishes this NDEATH message when the client disconnects abruptly
    (no MQTT DISCONNECT packet). This is Sparkplug B "Lite" semantics.
    """
    data_topic = f"novamex/ibarra/{area}/{machine}/data"
    health_topic = f"novamex/ibarra/{area}/{machine}/health"

    client = mqtt.Client(
        client_id=f"sim_{area}_{machine}",
        protocol=mqtt.MQTTv311,
    )

    # Sparkplug B Lite: LWT → NDEATH detection
    ndeath_payload = json.dumps({
        "node_health": "OFFLINE",
        "timestamp": int(time.time() * 1000),
    })
    client.will_set(data_topic, ndeath_payload, qos=1, retain=False)

    def connect_and_loop():
        try:
            client.connect(BROKER_HOST, BROKER_PORT, keepalive=30)
            client.loop_start()

            # Publish initial ONLINE health status (retained so subscribers
            # always have the latest known state)
            health_payload = json.dumps({
                "node_health": "ONLINE",
                "timestamp": int(time.time() * 1000),
            })
            client.publish(health_topic, health_payload, qos=1, retain=True)

            last_publish = 0.0

            while not STOP_EVENT.is_set():
                now = time.time()

                # Debounce: only publish if debounce interval has elapsed
                if now - last_publish >= DEBOUNCE_INTERVAL:
                    payload = build_payload(area, machine, cfg)
                    # Ensure node_health is always set (redundant but defensive)
                    payload["node_health"] = "ONLINE"
                    client.publish(
                        data_topic,
                        json.dumps(payload),
                        qos=1,
                    )
                    last_publish = now

                # Sleep until the next heartbeat cycle, but wake periodically
                # so we can react to STOP_EVENT promptly.
                sleep_interval = max(0.1, HEARTBEAT_INTERVAL - (now - last_publish))
                STOP_EVENT.wait(sleep_interval)

        except Exception as exc:
            print(f"[ERROR] {area}/{machine}: {exc}")
        finally:
            client.loop_stop()
            client.disconnect()

    return client, connect_and_loop


# ---------------------------------------------------------------------------
# Main entrypoint
# ---------------------------------------------------------------------------

def main():
    print("[SIMULATOR] GEMA V3.0 — Starting 7 machine simulators...")
    print(f"[SIMULATOR] Broker: {BROKER_HOST}:{BROKER_PORT}")
    print(f"[SIMULATOR] Heartbeat: {HEARTBEAT_INTERVAL}s | Debounce: {DEBOUNCE_INTERVAL}s")

    threads = []

    for area, machines in MACHINES.items():
        for machine_name, cfg in machines.items():
            client, runner = create_machine_client(area, machine_name, cfg)
            t = threading.Thread(
                target=runner,
                daemon=True,
                name=f"{area}_{machine_name}",
            )
            t.start()
            threads.append(t)
            print(f"[SIMULATOR] Started: novamex/ibarra/{area}/{machine_name}")

    def shutdown(sig, frame):
        print("\n[SIMULATOR] Shutting down...")
        STOP_EVENT.set()
        for t in threads:
            t.join(timeout=5)
        print("[SIMULATOR] All machines stopped.")
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    # Keep main thread alive until shutdown signal
    try:
        while not STOP_EVENT.is_set():
            STOP_EVENT.wait(1)
    except KeyboardInterrupt:
        shutdown(None, None)


if __name__ == "__main__":
    main()
