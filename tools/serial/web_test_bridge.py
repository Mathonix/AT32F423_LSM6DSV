"""Local test-only byte bridge to a real USB/UART port (never used by deployed app)."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading
import serial

parser = argparse.ArgumentParser()
parser.add_argument('--port', required=True)
parser.add_argument('--http-port', type=int, default=8800)
args = parser.parse_args()
stream = None
lock = threading.Lock()
origins = {'http://127.0.0.1:8798', 'http://localhost:8798', 'https://gyro.233688.xyz'}

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def respond(self, data=b'', status=200):
        self.send_response(status)
        self.send_header('Access-Control-Allow-Origin', self.headers.get('Origin', 'http://127.0.0.1:8798'))
        self.send_header('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.send_header('Access-Control-Allow-Private-Network', 'true')
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def allowed(self): return self.headers.get('Origin') in origins or not self.headers.get('Origin')
    def do_OPTIONS(self): self.respond(status=204 if self.allowed() else 403)
    def do_GET(self):
        if not self.allowed(): self.respond(status=403); return
        if self.path == '/health': self.respond(b'real serial bridge'); return
        try:
            with lock:
                data = stream.read(min(max(stream.in_waiting, 1), 65536)) if stream else b''
            self.respond(data)
        except Exception as e: self.respond(str(e).encode(), 500)
    def do_POST(self):
        global stream
        if not self.allowed(): self.respond(status=403); return
        length = int(self.headers.get('Content-Length', '0'))
        if length > 1024: self.respond(status=413); return
        body = self.rfile.read(length)
        try:
            with lock:
                if self.path == '/open':
                    if stream: stream.close()
                    stream = serial.Serial(args.port, 2000000, timeout=.02)
                    stream.dtr = True; stream.rts = True; stream.reset_input_buffer()
                elif self.path == '/close':
                    if stream: stream.close()
                    stream = None
                elif self.path == '/write':
                    if not stream: raise RuntimeError('serial is closed')
                    stream.write(body)
                else: raise ValueError('unknown endpoint')
            self.respond()
        except Exception as e: self.respond(str(e).encode(), 500)

server = ThreadingHTTPServer(('127.0.0.1', args.http_port), Handler)
print(f'Real {args.port} byte bridge on 127.0.0.1:{args.http_port}', flush=True)
try: server.serve_forever()
finally:
    if stream: stream.close()
    server.server_close()
