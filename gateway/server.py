from __future__ import annotations
import json, os, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
from .unifi_gateway import config_from_env


def make_handler(store):
    class Handler(BaseHTTPRequestHandler):
        def _json(self, value, status=200):
            body = json.dumps(value, separators=(",", ":")).encode()
            self.send_response(status); self.send_header("Content-Type", "application/json"); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/api/health":
                self._json({"ok": store.last_error is None, "last_error": store.last_error, "poll_interval_sec": store.poll_interval})
            elif path == "/api/snapshot": self._json(store.get())
            elif path == "/api/refresh": self._json(store.refresh())
            else: self._json({"error": "not found"}, 404)
        def log_message(self, *_): pass
    return Handler


def main():
    store = config_from_env(); stop = threading.Event()
    threading.Thread(target=store.loop, args=(stop,), daemon=True).start()
    host = os.getenv("GATEWAY_BIND", "127.0.0.1"); port = int(os.getenv("GATEWAY_PORT", "8787"))
    print(f"UniFi gateway listening on {host}:{port}; read-only API")
    server = ThreadingHTTPServer((host, port), make_handler(store))
    try: server.serve_forever()
    except KeyboardInterrupt: pass
    finally: stop.set(); server.server_close()

if __name__ == "__main__": main()
