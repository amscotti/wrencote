#!/usr/bin/env python3
"""Measure health p99 with and without a concurrent slow-reader stream."""
import socket
import ssl
import statistics
import sys
import time

try:
    from h2.connection import H2Connection
    from h2.events import StreamEnded
except ImportError:
    print("skip: python h2 package unavailable")
    sys.exit(0)

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18100
COUNT = 98


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


def headers(conn, stream_id, path):
    conn.send_headers(
        stream_id,
        [(":method", "GET"), (":scheme", "https"), (":authority", "localhost"), (":path", path)],
        end_stream=True,
    )


def measure(slow):
    sock, conn = connect()
    started = time.monotonic()
    starts = {}
    if slow:
        headers(conn, 1, "/large")
        first_health = 3
    else:
        first_health = 1
    for i in range(COUNT):
        stream_id = first_health + i * 2
        starts[stream_id] = time.monotonic()
        headers(conn, stream_id, "/health")
    sock.sendall(conn.data_to_send())
    sock.settimeout(0.002)
    ended = set()
    while len(ended) < COUNT and time.monotonic() - started < 5.0:
        try:
            data = sock.recv(1024)
        except socket.timeout:
            continue
        if not data:
            break
        for event in conn.receive_data(data):
            if isinstance(event, StreamEnded) and event.stream_id in starts:
                ended.add(event.stream_id)
    sock.close()
    assert len(ended) == COUNT, (len(ended), COUNT)
    return sorted((time.monotonic() - starts[s]) for s in starts)


def p99(values):
    return values[max(0, int(len(values) * 0.99) - 1)]


baseline = measure(False)
slow = measure(True)
print(f"baseline p99={p99(baseline):.4f}s slow p99={p99(slow):.4f}s")
assert p99(slow) <= max(0.002, p99(baseline) * 2.0), (p99(baseline), p99(slow))
