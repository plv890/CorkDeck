#!/bin/sh
# Local ephemeral HTTPS server; never contacts Steam or uses an account.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
server_pid=
trap '[ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || :; rm -rf "$build"' EXIT HUP INT TERM
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
    -addext subjectAltName=DNS:localhost -keyout "$build/key.pem" -out "$build/ca.pem" >/dev/null 2>&1
cat > "$build/server.py" <<'PY'
import http.server,ssl,sys,threading
pair=threading.Barrier(2)
class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def do_GET(self):
        if self.path=='/pair':
            try: pair.wait(timeout=2)
            except threading.BrokenBarrierError:
                self.send_response(504);self.send_header('Content-Length','0');self.end_headers();return
        if self.path=='/redirect':
            self.send_response(302);self.send_header('Location','http://127.0.0.1/never-follow');self.send_header('Content-Length','0');self.end_headers();return
        body=b'x'*65536
        self.send_response(200)
        self.send_header('Content-Length',str(len(body)))
        self.end_headers()
        try: self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError,ssl.SSLError): pass
    def log_message(self,*args): pass
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(sys.argv[1],sys.argv[2])
server.socket=ctx.wrap_socket(server.socket,server_side=True)
with open(sys.argv[3],'w') as f: f.write(str(server.server_port))
server.serve_forever()
PY
python3 "$build/server.py" "$build/ca.pem" "$build/key.pem" "$build/port" &
server_pid=$!
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -g -fsanitize=address,undefined -no-pie -ffunction-sections -fdata-sections \
    $(sdl2-config --cflags) "$root/tests/steam_cdn_keepalive.c" "$root/source/steam/steam_wire.c" \
    -Wl,--gc-sections -lcurl -o "$build/cdn"
test -s "$build/port"
STEAM_TEST_CA="$build/ca.pem" "$build/cdn" "$(cat "$build/port")"
cc -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations -g -fsanitize=address,undefined -no-pie \
    "$root/tests/steam_cdn_parallel.c" "$root/source/steam/steam_wire.c" -lcurl -o "$build/parallel"
STEAM_TEST_CA="$build/ca.pem" "$build/parallel" "$(cat "$build/port")"
