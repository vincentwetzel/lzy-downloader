#!/usr/bin/env python3
"""
Simple HTTP server for testing purposes.
Serves files from the current directory on localhost:8000.
"""

import http.server
import socketserver
import os

PORT = 8000

class QuietHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        # Suppress log messages to avoid cluttering test output
        pass

def main():
    with socketserver.TCPServer(("", PORT), QuietHTTPRequestHandler) as httpd:
        print(f"Serving on port {PORT}")
        httpd.serve_forever()

if __name__ == "__main__":
    main()