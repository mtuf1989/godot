#!/usr/bin/env python3
"""Serve Godot web export with required COOP/COEP headers for SharedArrayBuffer."""
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 4444

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(Path(__file__).parent / "web_export"), **kwargs)

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

print(f"Serving at http://localhost:{PORT}")
print("Required headers: COOP=same-origin, COEP=require-corp")
print("Press Ctrl+C to stop")
HTTPServer(("", PORT), Handler).serve_forever()
