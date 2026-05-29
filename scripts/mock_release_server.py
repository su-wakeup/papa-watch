#!/usr/bin/env python3
"""
Mock GitHub Releases server — pretends to be api.github.com for OTA testing.

Usage:
    python3 scripts/mock_release_server.py 0.4.1
                                            ↑ version to advertise as "latest"

Endpoints:
    /releases/latest    → GitHub-API-shaped JSON with this version + firmware URL
    /firmware.bin       → the file at .pio/build/m5stopwatch/firmware.bin
"""
import http.server
import json
import os
import socket
import sys

VERSION       = sys.argv[1] if len(sys.argv) > 1 else "0.4.1"
FIRMWARE_PATH = ".pio/build/m5stopwatch/firmware.bin"
PORT          = 8000


def host_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/releases/latest":
            body = json.dumps({
                "tag_name": f"v{VERSION}",
                "assets": [{
                    "name": "firmware.bin",
                    "browser_download_url": f"http://{HOST}:{PORT}/firmware.bin",
                }],
            }).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/firmware.bin":
            if not os.path.exists(FIRMWARE_PATH):
                self.send_response(404); self.end_headers()
                return
            size = os.path.getsize(FIRMWARE_PATH)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(size))
            self.end_headers()
            with open(FIRMWARE_PATH, "rb") as f:
                while True:
                    chunk = f.read(4096)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
        else:
            self.send_response(404); self.end_headers()

    def log_message(self, fmt, *args):
        print(f"  → {self.address_string()}  {fmt % args}")


HOST = host_ip()
size = os.path.getsize(FIRMWARE_PATH) if os.path.exists(FIRMWARE_PATH) else 0
print(f"Mock OTA server: http://{HOST}:{PORT}")
print(f"  advertising:   v{VERSION}")
print(f"  firmware.bin:  {size} bytes  ({FIRMWARE_PATH})")
print(f"  endpoints:     /releases/latest   /firmware.bin")
http.server.HTTPServer(("", PORT), Handler).serve_forever()
