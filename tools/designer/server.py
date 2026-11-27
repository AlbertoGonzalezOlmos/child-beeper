"""Loopback-only editor server. Build/flash accepts design data, never shell commands."""
from __future__ import annotations

import argparse
import functools
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import secrets
import signal
import subprocess
import sys
import threading
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.cbchecks.ports import guard_port, list_ports  # noqa: E402
from tools.designer import compiler, schema  # noqa: E402

PROJECT = ROOT / "design/project.json"
STATIC = Path(__file__).parent


class BusyError(ValueError):
    pass


class Jobs:
    def __init__(self, build_root):
        self.build_root = build_root
        self.lock = threading.RLock()
        self.state = dict(busy=False, phase="Ready", log="", result=None, report=None, project="")

    def snapshot(self):
        with self.lock:
            return dict(self.state)

    def log(self, line):
        with self.lock:
            self.state["log"] = (self.state["log"] + line)[-64000:]

    def save(self, source):
        with self.lock:
            if self.state["busy"]:
                raise BusyError("A build is running. Wait for it to finish before saving.")
            project, _, _, report = compiler.compile_project(source)
            compiler.atomic_write(PROJECT, json.dumps(project, indent=2, ensure_ascii=False) + "\n")
            return dict(project=project, report=report)

    def start(self, source, action, port=None):
        with self.lock:
            if self.state["busy"]:
                raise BusyError("A build is already running.")
            # Validate the port before saving anything or starting a flash job.
            selected = guard_port(port) if action == "flash" else None
            self.state.update(busy=True, phase="Validating", log="", result=None, report=None,
                              project=str(source.get("name", ""))[:80])
        threading.Thread(target=self.run, args=(source, action, selected), daemon=False).start()
        return self.snapshot()

    def run(self, source, action, port):
        timer = None
        try:
            project, _, _, report = compiler.compile_project(source)
            compiler.atomic_write(PROJECT, json.dumps(project, indent=2, ensure_ascii=False) + "\n")
            with self.lock:
                self.state.update(phase="Building and flashing" if action == "flash" else "Building",
                                  report=report, project=project["name"])
            command = [str(ROOT / "cb"), "build", "app"] if action == "build" else [
                str(ROOT / "cb"), "flash", "--target", "app", "--port", port]
            env = dict(os.environ, CB_BUILD_ROOT=str(self.build_root))
            with subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, start_new_session=True) as process:
                def deadline():
                    self.log("\nBuild exceeded 20 minutes; stopping the owned build process.\n")
                    try:
                        os.killpg(process.pid, signal.SIGINT)
                    except ProcessLookupError:
                        pass
                timer = threading.Timer(1200, deadline)
                timer.daemon = True
                timer.start()
                for line in process.stdout:
                    self.log(line)
                result = process.wait()
            image = self.build_root / "app/child_beeper.bin"
            if result == 0 and image.exists():
                report["firmwareBytes"] = image.stat().st_size
            with self.lock:
                self.state.update(result=result, report=report,
                                  phase=("Flashed successfully" if action == "flash" else "Build complete")
                                  if result == 0 else "Build failed")
        except Exception as error:
            self.log(f"{type(error).__name__}: {error}\n")
            with self.lock:
                self.state.update(phase="Failed", result=1)
        finally:
            if timer:
                timer.cancel()
            with self.lock:
                self.state["busy"] = False


@functools.lru_cache(maxsize=6)
def builtin_preview(name):
    return compiler.font_preview(compiler.builtin_font(schema.FONTS.index(name)))


class EditorServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, build_root):
        super().__init__(address, Handler)
        self.token = secrets.token_hex(32)
        self.jobs = Jobs(build_root)


class Handler(BaseHTTPRequestHandler):
    server: EditorServer

    def log_message(self, format, *args):
        # No payloads, project assets, credentials or session tokens in request logs.
        pass

    def allowed_host(self):
        host = self.headers.get("Host", "").lower()
        port = self.server.server_port
        return host in (f"127.0.0.1:{port}", f"localhost:{port}")

    def send(self, payload, status=200, content_type="application/json"):
        if content_type == "application/json":
            payload = json.dumps(payload, ensure_ascii=False).encode()
        elif isinstance(payload, str):
            payload = payload.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy",
                         "default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; "
                         "script-src 'self'; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'self'")
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        if not self.allowed_host():
            self.send(dict(error="Use the localhost editor URL."), 403)
            return
        path = urlsplit(self.path).path
        try:
            if path == "/api/bootstrap":
                project = schema.normalize(json.loads(PROJECT.read_text()))
                self.send(dict(token=self.server.token, project=project, defaults=schema.default_project(), ports=list_ports(),
                               pins=schema.GPIO_PINS, fonts=schema.FONTS, bindings=schema.BINDINGS,
                               actions=schema.ACTIONS, status=self.server.jobs.snapshot()))
            elif path == "/api/status":
                self.send(dict(status=self.server.jobs.snapshot(), ports=list_ports()))
            elif path.startswith("/api/fonts/") and path.removeprefix("/api/fonts/") in schema.FONTS:
                self.send(builtin_preview(path.removeprefix("/api/fonts/")))
            elif path in ("/", "/app.js", "/style.css"):
                name, content_type = {
                    "/": ("index.html", "text/html; charset=utf-8"),
                    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
                    "/style.css": ("style.css", "text/css; charset=utf-8"),
                }[path]
                self.send((STATIC / name).read_bytes(), content_type=content_type)
            else:
                self.send(dict(error="Not found."), 404)
        except (OSError, ValueError) as error:
            self.send(dict(error=str(error)), 400)

    def do_POST(self):
        if not self.allowed_host() or not secrets.compare_digest(
                self.headers.get("X-Designer-Token", ""), self.server.token):
            self.send(dict(error="Editor session expired or unauthorized. Reload the localhost editor."), 403)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= schema.MAX_PROJECT_BYTES:
                self.send(dict(error="Request must be nonempty and at most 16 MB."), 413)
                return
            if self.headers.get_content_type() != "application/json":
                self.send(dict(error="Expected application/json."), 415)
                return
            self.connection.settimeout(15)
            data = json.loads(self.rfile.read(length))
            data = schema.object_value(data, "Request")
            path = urlsplit(self.path).path
            if path == "/api/asset-preview":
                project = schema.normalize(data.get("project"))
                asset = next((a for a in project["assets"] if a["id"] == data.get("id")), None)
                if not asset:
                    raise schema.DesignError("Asset not found.")
                self.send(compiler.image_preview(asset) if asset["kind"] == "image" else
                          compiler.font_preview(compiler.custom_font(asset, project)))
            elif path == "/api/validate":
                project, _, _, report = compiler.compile_project(data.get("project"))
                self.send(dict(project=project, report=report))
            elif path == "/api/save":
                self.send(self.server.jobs.save(data.get("project")))
            elif path in ("/api/build", "/api/flash"):
                source = schema.object_value(data.get("project"), "Project")
                port = data.get("port") or None
                if port is not None and not isinstance(port, str):
                    raise schema.DesignError("Serial port must be text.")
                self.send(self.server.jobs.start(source, path.rsplit("/", 1)[1], port), 202)
            else:
                self.send(dict(error="Not found."), 404)
        except BusyError as error:
            self.send(dict(error=str(error)), 409)
        except (ValueError, OSError) as error:
            self.send(dict(error=str(error)), 400)
        except Exception as error:
            # Port guard errors and decoder errors are recoverable UI errors.
            self.send(dict(error=str(error)), 400)


def main():
    parser = argparse.ArgumentParser(description="Child Beeper design studio (local browser + USB build/flash).")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--build-root", type=Path, default=ROOT / "build/designer")
    args = parser.parse_args()
    build_root = args.build_root
    if not build_root.is_absolute():
        build_root = ROOT / build_root
    server = EditorServer(("127.0.0.1", args.port), build_root)
    print(f"Child Beeper Studio: http://127.0.0.1:{server.server_port}", flush=True)
    print("External GPIO outputs stay disabled until explicitly configured in the project.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping editor. Any active build/flash will finish before exit.", flush=True)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
