#!/usr/bin/env python3
"""Демо SPNEGO-сервис для проверки Kerberos (ADR-122).

Проверяет `Authorization: Negotiate <токен>` через GSSAPI и отдаёт JSON — так же, как это
делает настоящий сервис за Kerberos-прокси. Требует keytab и работающий KDC:

    KRB5_CONFIG=/tmp/rep-krb/krb5.conf KRB5_KTNAME=/tmp/rep-krb/http.keytab \
        python3 scripts/rest-spnego-demo.py
"""
import base64
import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import gssapi

SPN = os.environ.get("SPNEGO_SPN", "HTTP@localhost")
PORT = int(os.environ.get("SPNEGO_PORT", "8098"))

try:
    creds = gssapi.Credentials(
        usage="accept", name=gssapi.Name(SPN, gssapi.NameType.hostbased_service)
    )
except Exception as exc:  # noqa: BLE001
    print(f"нет учётных данных для {SPN}: {exc}", file=sys.stderr)
    sys.exit(2)


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):  # тише в тестах
        pass

    def _send(self, code, body=None, headers=None):
        payload = b"" if body is None else json.dumps(body, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if payload:
            self.wfile.write(payload)

    def do_GET(self):  # noqa: N802
        if self.path != "/api/negotiated":
            self._send(404, {"error": "not found", "path": self.path})
            return
        header = self.headers.get("Authorization", "")
        contexts = self.server.contexts
        key = id(self.connection)
        if not header.startswith("Negotiate "):
            contexts.pop(key, None)
            self._send(401, {"error": "unauthorized"}, {"WWW-Authenticate": "Negotiate"})
            return
        raw = header[len("Negotiate ") :].strip()
        try:
            token = base64.b64decode(raw) if raw else b""
        except Exception:  # noqa: BLE001
            self._send(401, {"error": "bad token"}, {"WWW-Authenticate": "Negotiate"})
            return
        context = contexts.get(key)
        if context is None:
            context = gssapi.SecurityContext(creds=creds, usage="accept")
            contexts[key] = context
        try:
            out = context.step(token)
        except Exception as exc:  # noqa: BLE001
            contexts.pop(key, None)
            self._send(401, {"error": str(exc)}, {"WWW-Authenticate": "Negotiate"})
            return
        if not context.complete:
            challenge = base64.b64encode(out or b"").decode()
            self._send(401, {"error": "continue"}, {"WWW-Authenticate": f"Negotiate {challenge}"})
            return
        contexts.pop(key, None)
        user = str(context.initiator_name) if context.initiator_name else ""
        self._send(
            200,
            {
                "items": [{"tenant": user or "anonymous", "value": 42}],
                "total": 1,
                "user": user,
                "spn": SPN,
            },
        )


if __name__ == "__main__":
    server = ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
    server.contexts = {}
    print(f"SPNEGO-демо на http://127.0.0.1:{PORT}/api/negotiated, SPN {SPN}", flush=True)
    server.serve_forever()
