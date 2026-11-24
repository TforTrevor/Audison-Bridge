"""Offline dashboard preview backed by an explicitly simulated ESP32.

Run: python tools/preview_dashboard.py
This never connects to, updates, or reboots hardware.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import time
from urllib.parse import urlsplit, parse_qs

ROOT = Path(__file__).resolve().parents[1]
started = time.monotonic()
generation = 1


class Preview(BaseHTTPRequestHandler):
    def send(self, payload, status=200, mime="application/json"):
        data = payload.encode() if isinstance(payload, str) else json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        route = urlsplit(self.path)
        ms = int((time.monotonic() - started) * 1000)
        boot_id = f"demo-boot-{generation}"
        lines = [
            {"id": 1, "ms": 200, "text": "Audison Bridge boot: initializing CAN and RS232 before Wi-Fi"},
            {"id": 2, "ms": 203, "text": "Ready: CAN 100 kbps, volume 0x183, mute 0x181, listen-only (car)"},
            {"id": 3, "ms": 206, "text": "CAN controller state: 1"},
            {"id": 4, "ms": 480, "text": "Dashboard ready: http://192.168.4.1 (or http://audison-bridge.local)"},
            {"id": 5, "ms": 830, "text": "CAN mute received: OFF"},
            {"id": 6, "ms": 850, "text": "CAN volume received: 20 (raw 0x30) -> Audison: -30.0 dB (0x6C)"},
            {"id": 7, "ms": 851, "text": "Audison TX: 01 04 00 01 6C 00 72"},
        ]
        for i in range(min(300, ms // 2500)):
            lines.append({"id": i + 8, "ms": 1000 + i * 2500,
                          "text": f"[SIMULATED] CAN volume received: {20 + i % 3} -> Audison command sent"})
        lines = lines[-256:]
        if route.path == "/":
            html = (ROOT / "dashboard/index.html").read_text(encoding="utf-8")
            html = html.replace("<body>", '<body><div style="padding:10px;text-align:center;background:#493817;color:#ffda99;font:14px system-ui">PREVIEW · Simulated ESP32 readings · No hardware connected</div>')
            self.send(html, mime="text/html; charset=utf-8")
        elif route.path == "/api/session":
            self.send({"bootId": boot_id, "token": "preview-token"})
        elif route.path == "/api/stats":
            self.send(dict(bootId=boot_id, firmware="1.0.0-wifi-preview", build="Preview / simulated device",
                           uptimeMs=ms, resetReason="Power on (simulated)", chip="ESP32-D0WDQ6", revision=1,
                           cores=2, cpuMHz=240, heapFree=168480, heapMin=151220, heapLargest=110592,
                           flashBytes=4194304, sketchBytes=1080840, otaBytes=1966080, partition="app0",
                           ssid="Audison-Bridge-DEMO", ip="192.168.4.1", clients=1, maintenance=False,
                           rebootPending=False, canReady=True, listenOnly=True, canState=1,
                           canFrames=int(ms * .12), canAgeMs=12, rxMissed=0, rxOverruns=0,
                           busErrors=0, rxErrors=0, txErrors=0, rxQueued=0, serialPackets=len(lines)//2,
                           serialRxBytes=0, loops=int(ms * .7), maxLoopUs=1350,
                           volume=20, muted=False, audisonDb=-30.0, logOldest=lines[0]["id"],
                           logLatest=lines[-1]["id"], logRetained=len(lines), logCapacity=256))
        elif route.path == "/api/logs":
            after = int(parse_qs(route.query).get("after", ["0"])[0])
            missed = max(0, lines[0]["id"] - after - 1) if after else 0
            batch = [line for line in lines if line["id"] > after][:32]
            self.send(dict(bootId=boot_id, oldest=lines[0]["id"], latest=lines[-1]["id"], missed=missed,
                           next=batch[-1]["id"] if batch else lines[-1]["id"], lines=batch))
        elif route.path == "/api/logs.txt":
            self.send("SIMULATED DEVICE HISTORY\n" + "\n".join(f'[{line["ms"]} ms] {line["text"]}' for line in lines), mime="text/plain")
        else:
            self.send({"error": "Not found"}, 404)

    def do_POST(self):
        # The preview intentionally cannot claim to have verified a real image.
        if self.path == "/api/firmware":
            self.rfile.read(int(self.headers.get("Content-Length", 0)))
            self.send({"error": "Preview only. No firmware was installed."}, 400)
        elif self.path == "/api/reboot":
            global started, generation
            started = time.monotonic()
            generation += 1
            self.send({"ok": True, "rebooting": True})
        else:
            self.send({"error": "Not found"}, 404)

    def log_message(self, *_args):
        pass


if __name__ == "__main__":
    print("Simulated ESP32 preview: http://127.0.0.1:8765", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 8765), Preview).serve_forever()
