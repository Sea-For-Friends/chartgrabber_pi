#!/usr/bin/env python3
"""Capture-only stub: answers every GET with 403 Forbidden and an empty body, on 127.0.0.1:8766.
Prints nothing; stops on SIGTERM."""
import signal
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class Refuse(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(403)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *args):
        pass


def stop(*_):
    sys.exit(0)


signal.signal(signal.SIGTERM, stop)
HTTPServer(("127.0.0.1", 8766), Refuse).serve_forever()
