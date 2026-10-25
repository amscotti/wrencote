#!/usr/bin/env python3
"""Slow-reader probe: one large stream must not stall a small stream."""
import socket
import ssl
import sys
import time

try:
    from h2.connection import H2Connection
    from h2.events import DataReceived, ResponseReceived, StreamEnded
except ImportError:
    print("skip: python h2 package unavailable")
    sys.exit(0)

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18099


def connect():
    context = ssl._create_unverified_context()
    context.set_alpn_protocols(["h2"])
    raw = socket.create_connection((HOST, PORT))
    sock = context.wrap_socket(raw, server_hostname="localhost")
    assert sock.selected_alpn_protocol() == "h2"
    conn = H2Connection()
    conn.initiate_connection()
    sock.sendall(conn.data_to_send())
    return sock, conn


def request(conn, stream_id, path):
    conn.send_headers(
        stream_id,
        [(":method", "GET"), (":scheme", "https"), (":authority", "localhost"), (":path", path)],
        end_stream=True,
    )


def timed_health(with_large):
    sock, conn = connect()
    if with_large:
        request(conn, 1, "/large")
        request(conn, 3, "/health")
    else:
        request(conn, 1, "/health")
    sock.sendall(conn.data_to_send())
    sock.settimeout(0.002)
    health_stream = 3 if with_large else 1
    started = time.monotonic()
    first_byte = None
    ended = False
    while time.monotonic() - started < 2.0 and not ended:
        try:
            data = sock.recv(1024)
        except socket.timeout:
            continue
        if not data:
            break
        events = conn.receive_data(data)
        for event in events:
            if isinstance(event, ResponseReceived) and event.stream_id == health_stream:
                first_byte = time.monotonic() - started
            if isinstance(event, DataReceived) and event.stream_id == health_stream and first_byte is None:
                first_byte = time.monotonic() - started
            if isinstance(event, StreamEnded) and event.stream_id == health_stream:
                ended = True
    sock.close()
    return first_byte, ended


baseline, baseline_ok = timed_health(False)
slow, slow_ok = timed_health(True)
print(f"baseline={baseline:.4f}s ok={baseline_ok} slow_reader={slow:.4f}s ok={slow_ok}")
assert baseline_ok and slow_ok
if baseline is not None and slow is not None:
    assert slow <= max(0.25, baseline * 2.0), (baseline, slow)
