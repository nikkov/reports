#!/usr/bin/env python3
"""Минимальный SMTP-приёмник для проверки рассылки (dev, не для продакшена).

Слушает порт, принимает письма и складывает их файлами в каталог:
  python3 scripts/smtp-sink.py --port 1025 --dir /tmp/rep-mail

Реализованы только нужные команды: EHLO/HELO, MAIL FROM, RCPT TO, DATA, RSET, NOOP, QUIT.
"""
import argparse
import os
import socket
import threading
import time


def handle(conn, addr, out_dir):
    def send(line):
        conn.sendall((line + "\r\n").encode())

    buf = b""
    file = conn.makefile("rb")
    send("220 rep-sink ESMTP")
    sender = ""
    recipients = []
    while True:
        line = file.readline()
        if not line:
            break
        text = line.decode("utf-8", "replace").strip()
        upper = text.upper()
        if upper.startswith("EHLO"):
            send("250-rep-sink")
            send("250 SIZE 26214400")
        elif upper.startswith("HELO"):
            send("250 rep-sink")
        elif upper.startswith("MAIL FROM:"):
            sender = text[10:].strip(" <>")
            recipients = []
            send("250 OK")
        elif upper.startswith("RCPT TO:"):
            recipients.append(text[8:].strip(" <>"))
            send("250 OK")
        elif upper == "DATA":
            send("354 End data with <CR><LF>.<CR><LF>")
            chunks = []
            while True:
                chunk = file.readline()
                if chunk in (b".\r\n", b".\n", b""):
                    break
                chunks.append(chunk)
            body = b"".join(chunks)
            stamp = time.strftime("%Y%m%d-%H%M%S")
            name = os.path.join(out_dir, f"{stamp}-{abs(hash((sender, tuple(recipients)))) % 10000}.eml")
            with open(name, "wb") as fh:
                fh.write(body)
            print(f"принято письмо: from={sender} to={recipients} → {name}", flush=True)
            send("250 OK queued")
        elif upper == "RSET":
            send("250 OK")
        elif upper == "NOOP":
            send("250 OK")
        elif upper == "QUIT":
            send("221 Bye")
            break
        else:
            send("250 OK")
    try:
        file.close()
    except Exception:
        pass
    conn.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=1025)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--dir", default="/tmp/rep-mail")
    args = parser.parse_args()
    os.makedirs(args.dir, exist_ok=True)
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(8)
    print(f"SMTP-приёмник на {args.host}:{args.port}, письма в {args.dir}", flush=True)
    while True:
        conn, addr = server.accept()
        threading.Thread(target=handle, args=(conn, addr, args.dir), daemon=True).start()


if __name__ == "__main__":
    main()
