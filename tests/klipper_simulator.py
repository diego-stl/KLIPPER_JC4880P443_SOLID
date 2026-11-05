"""Local Moonraker protocol fixture. Never connects to a printer.

Run: python tests/klipper_simulator.py --port 17125
Self-test: python tests/klipper_simulator.py --self-test
Admin POST /__test/control accepts state, job_state, filename, progress,
delay_post, drop_post, reject_post, malformed_post, truncated_png, print_duration,
metadata_overrides. Use delta to emit partial and optional timelapse fields.
GET /__test/requests returns received POST requests (including uncertain ones).
The bind default is loopback; --bind 0.0.0.0 is explicit for a device on LAN.
"""
import argparse
import base64
import hashlib
import json
import socket
import struct
import threading
import time
import zlib
from http.client import IncompleteRead, RemoteDisconnected
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.request import Request, urlopen
from urllib.parse import parse_qs, urlsplit


def chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data) & 0xffffffff)


def make_png(size):
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', size, size, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress((b'\0' + b'\x00\xc9\xf4' * size) * size)) + chunk(b'IEND', b''))


PNG = make_png(100)
PNG300 = make_png(300)
PNG48 = make_png(48)


class Fixture:
    def __init__(self):
        self.lock = threading.RLock()
        self.clients = set()
        self.requests = []
        self.options = dict(state='ready', job_state='standby', filename='', progress=0,
                            delay_post=0, drop_post=False, reject_post=False,
                            malformed_post=False, truncated_png=False, fragment_ws=False,
                            metadata_overrides={}, print_duration=120, timelapse=False,
                            timelapse_paused=False, takingframe=False)

    def status(self):
        with self.lock:
            o = self.options.copy()
        result = dict(extruder=dict(temperature=205, target=210), heater_bed=dict(temperature=58, target=60),
                    print_stats=dict(filename=o['filename'], state=o['job_state'], print_duration=o['print_duration']),
                    virtual_sdcard=dict(progress=o['progress']),
                    webhooks=dict(state=o['state'], state_message='Simulated Klipper: ' + o['state']),
                    toolhead=dict(homed_axes='xyz', position=[100, 80, 5, 0],
                                  axis_minimum=[0, 0, 0, 0], axis_maximum=[300, 250, 250, 0]))
        if o['timelapse']:
            result['gcode_macro TIMELAPSE_TAKE_FRAME'] = dict(is_paused=o['timelapse_paused'], takingframe=o['takingframe'])
        return result

    def update(self, changes):
        with self.lock:
            self.options.update({k: v for k, v in changes.items() if k in self.options})
            clients = list(self.clients)
        if changes.get('disconnect_ws'):
            for c in clients:
                try:
                    c.connection.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
            return
        for client in clients:
            try:
                if 'state' in changes:
                    method = {'ready': 'notify_klippy_ready', 'shutdown': 'notify_klippy_shutdown',
                              'disconnected': 'notify_klippy_disconnected'}.get(changes['state'])
                    if method:
                        client.ws_send(dict(jsonrpc='2.0', method=method, params=[]))
                client.ws_send(dict(jsonrpc='2.0', method='notify_status_update',
                                    params=[changes.get('delta', self.status()), changes.get('eventtime', time.monotonic())]))
            except OSError:
                pass


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, fmt, *args):
        pass

    @property
    def fixture(self):
        return self.server.fixture

    def reply(self, value, status=200):
        data = json.dumps(value).encode()
        self.send_response(status)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == '/__test/identity':
            return self.reply(dict(service='JC4880P443_MOONRAKER_SIMULATOR', test_only=True))
        if self.path == '/websocket':
            return self.websocket()
        if self.path == '/__test/requests':
            with self.fixture.lock:
                return self.reply(self.fixture.requests[:])
        if self.path.startswith('/server/files/list'):
            return self.reply(dict(result=[dict(path=f'folder/part {i}.gcode', modified=i) for i in range(25)]))
        if self.path == '/printer/objects/list':
            optional = ['gcode_macro TIMELAPSE_TAKE_FRAME'] if self.fixture.options['timelapse'] else []
            return self.reply(dict(result=dict(objects=optional + ['extruder', 'heater_bed', 'exclude_object'] + [f'gcode_macro _HELPER_{i}' for i in range(25)] + [f'gcode_macro TEST_{i}' for i in range(25)])))
        if self.path.startswith('/server/files/metadata'):
            metadata = dict(estimated_time=3600, filament_total=12345.6,
                            filament_weight_total=37.8, filament_colors=['#00C9F4', '#FF9D00'],
                            filament_weights=[12.4, 25.4], referenced_tools=[0, 1],
                            thumbnails=[dict(width=size, height=size, relative_path=f'.thumbs/test-{size}.png')
                                        for size in [48, 100, 300]])
            with self.fixture.lock:
                metadata.update(self.fixture.options['metadata_overrides'])
            return self.reply(dict(result=metadata))
        if self.path.startswith('/server/files/gcodes/'):
            png = PNG300 if self.path.endswith('test-300.png') else PNG48 if self.path.endswith('test-48.png') else PNG
            self.send_response(200)
            self.send_header('Content-Type', 'image/png')
            self.send_header('Content-Length', str(len(png)))
            self.end_headers()
            with self.fixture.lock:
                truncated = self.fixture.options['truncated_png']
            self.wfile.write(png[:-15] if truncated else png)
            if truncated:
                self.close_connection = True
            return
        if self.path.startswith('/printer/objects/query?exclude_object'):
            return self.reply(dict(result=dict(status=dict(exclude_object=dict(
                current_object='part_1', excluded_objects=['part_2'],
                objects=[dict(name=f'part_{i}', center=[10+i*10, 20+i*5],
                              polygon=[[i*10, i*5], [i*10+20, i*5], [i*10+20, i*5+20]]) for i in range(20)])))))
        if self.path.startswith('/printer/objects/query?'):
            names = parse_qs(urlsplit(self.path).query, keep_blank_values=True)
            state = {}
            for name in names:
                if name == 'gcode_macro TIMELAPSE_TAKE_FRAME' and self.fixture.options['timelapse']:
                    state[name] = dict(is_paused=self.fixture.options['timelapse_paused'], takingframe=self.fixture.options['takingframe'])
                elif name.startswith('gcode_macro '):
                    state[name] = ({'portal_visible': False} if name == 'gcode_macro TEST_0' else
                                   {'portal_visible': True} if name == 'gcode_macro TEST_1' else {})
            return self.reply(dict(result=dict(status=state)))
        self.reply(dict(error=dict(message='Unknown fixture endpoint')), 404)

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        try:
            body = json.loads(raw) if raw else {}
        except ValueError:
            return self.reply(dict(error=dict(message='Invalid JSON')), 400)
        if self.path == '/__test/control':
            self.fixture.update(body)
            return self.reply(dict(result='ok'))
        with self.fixture.lock:
            self.fixture.requests.append(dict(path=self.path, body=body, at=time.monotonic()))
            options = self.fixture.options.copy()
        if self.path not in ('/printer/gcode/script', '/printer/emergency_stop', '/machine/shutdown'):
            return self.reply(dict(error=dict(message='Unknown POST endpoint')), 404)
        # Log first: a dropped response simulates a request which may already have executed.
        time.sleep(max(0, float(options['delay_post'])))
        if options['drop_post']:
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        if options['reject_post']:
            return self.reply(dict(error=dict(message='Fixture rejection')), 400)
        if options['malformed_post']:
            self.send_response(200)
            self.send_header('Content-Length', '7')
            self.end_headers()
            self.wfile.write(b'{broken')
            return
        self.reply(dict(result='ok'))

    def ws_send(self, value):
        data = json.dumps(value).encode()
        with self.send_lock:
            with self.fixture.lock:
                fragment = self.fixture.options['fragment_ws']
            if fragment:
                middle = len(data) // 2
                self.ws_frame(data[:middle], 1, False)
                self.ws_frame(data[middle:], 0, True)
            else:
                self.ws_frame(data, 1, True)

    def ws_frame(self, data, opcode, final):
        header = bytes([(0x80 if final else 0) | opcode])
        length = len(data)
        header += bytes([length]) if length < 126 else b'\x7e' + struct.pack('!H', length)
        self.wfile.write(header + data)
        self.wfile.flush()

    def websocket(self):
        key = self.headers.get('Sec-WebSocket-Key')
        if not key:
            return self.reply(dict(error='WebSocket key required'), 400)
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        self.send_header('Upgrade', 'websocket')
        self.send_header('Connection', 'Upgrade')
        self.send_header('Sec-WebSocket-Accept', accept)
        self.end_headers()
        self.send_lock = threading.Lock()
        with self.fixture.lock:
            self.fixture.clients.add(self)
        try:
            while True:
                header = self.rfile.read(2)
                if len(header) != 2:
                    break
                opcode, length = header[0] & 15, header[1] & 127
                if length == 126:
                    length = struct.unpack('!H', self.rfile.read(2))[0]
                elif length == 127:
                    length = struct.unpack('!Q', self.rfile.read(8))[0]
                if length > 65536:
                    break
                mask = self.rfile.read(4) if header[1] & 128 else None
                data = self.rfile.read(length)
                if mask:
                    data = bytes(c ^ mask[i % 4] for i, c in enumerate(data))
                if opcode == 8:
                    break
                if opcode == 9:
                    with self.send_lock:
                        self.ws_frame(data, 10, True)
                    continue
                if opcode != 1:
                    continue
                request = json.loads(data)
                method = request.get('method')
                if method == 'server.info':
                    result = dict(klippy_state=self.fixture.options['state'])
                elif method == 'printer.info':
                    result = dict(state=self.fixture.options['state'], state_message='Simulated Klipper')
                elif method == 'printer.objects.subscribe':
                    result = dict(status=self.fixture.status(), eventtime=time.monotonic())
                else:
                    self.ws_send(dict(jsonrpc='2.0', id=request.get('id'), error=dict(code=-32601, message='Unknown method')))
                    continue
                self.ws_send(dict(jsonrpc='2.0', id=request.get('id'), result=result))
        except (OSError, ValueError, struct.error):
            pass
        finally:
            with self.fixture.lock:
                self.fixture.clients.discard(self)
            self.close_connection = True


def make_server(bind='127.0.0.1', port=17125):
    server = ThreadingHTTPServer((bind, port), Handler)
    server.daemon_threads = True
    server.fixture = Fixture()
    return server


def self_test():
    server = make_server(port=0)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://127.0.0.1:{server.server_port}'

    def get(path):
        return json.load(urlopen(base + path, timeout=2))

    def post(path, body):
        return json.load(urlopen(Request(base + path, json.dumps(body).encode(), {'Content-Type': 'application/json'}), timeout=2))

    try:
        assert get('/__test/identity') == dict(service='JC4880P443_MOONRAKER_SIMULATOR', test_only=True)
        assert len(get('/server/files/list?root=gcodes')['result']) == 25
        assert len(get('/printer/objects/list')['result']['objects']) == 53
        assert get('/printer/objects/query?exclude_object')['result']['status']['exclude_object']['current_object'] == 'part_1'
        metadata = get('/server/files/metadata?filename=x')['result']
        assert metadata['filament_weight_total'] == 37.8
        assert metadata['filament_weights'] == [12.4, 25.4]
        assert metadata['referenced_tools'] == [0, 1]
        assert [t['width'] for t in metadata['thumbnails']] == [48, 100, 300]
        assert urlopen(base + '/server/files/gcodes/.thumbs/test-300.png').read() == PNG300
        macros = get('/printer/objects/query?gcode_macro%20TEST_0=portal_visible&gcode_macro%20TEST_2=portal_visible')['result']['status']
        assert macros['gcode_macro TEST_0']['portal_visible'] is False
        assert macros['gcode_macro TEST_2'] == {}
        post('/__test/control', dict(metadata_overrides={'filament_weight_total': 0, 'filament_weights': [0, None]}))
        zero = get('/server/files/metadata?filename=x')['result']
        assert zero['filament_weight_total'] == 0 and zero['filament_weights'][1] is None
        post('/__test/control', dict(metadata_overrides={}, timelapse=True, takingframe=True))
        flags = get('/printer/objects/query?gcode_macro%20TIMELAPSE_TAKE_FRAME=is_paused,takingframe')['result']['status']
        assert flags['gcode_macro TIMELAPSE_TAKE_FRAME'] == dict(is_paused=False, takingframe=True)
        assert 'gcode_macro TIMELAPSE_TAKE_FRAME' in get('/printer/objects/list')['result']['objects']
        post('/__test/control', dict(timelapse=False, takingframe=False))
        assert urlopen(base + '/server/files/gcodes/.thumbs/test.png').read() == PNG
        assert post('/printer/gcode/script', dict(script='M105'))['result'] == 'ok'
        assert len(get('/__test/requests')) == 1
        post('/__test/control', dict(reject_post=True))
        try:
            post('/printer/gcode/script', dict(script='REJECT_TEST'))
            raise AssertionError('Rejection fixture did not reject')
        except HTTPError as err:
            assert err.code == 400
        post('/__test/control', dict(reject_post=False, malformed_post=True))
        try:
            post('/printer/gcode/script', dict(script='MALFORMED_TEST'))
            raise AssertionError('Malformed fixture returned JSON')
        except ValueError:
            pass
        post('/__test/control', dict(malformed_post=False, drop_post=True))
        try:
            post('/printer/gcode/script', dict(script='UNCERTAIN_TEST'))
            raise AssertionError('Uncertain fixture unexpectedly confirmed')
        except RemoteDisconnected:
            pass
        assert sum(r['body'].get('script') == 'UNCERTAIN_TEST' for r in get('/__test/requests')) == 1
        post('/__test/control', dict(drop_post=False, truncated_png=True))
        try:
            urlopen(base + '/server/files/gcodes/.thumbs/test.png', timeout=2).read()
            raise AssertionError('Truncated fixture unexpectedly completed')
        except IncompleteRead:
            pass
        post('/__test/control', dict(truncated_png=False))
        # Standard library WebSocket handshake + masked JSON-RPC bootstrap.
        sock = socket.create_connection(('127.0.0.1', server.server_port), timeout=2)
        stream = sock.makefile('rwb', buffering=0)
        stream.write(b'GET /websocket HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n')
        assert b'101' in stream.readline()
        while stream.readline() != b'\r\n':
            pass
        def rpc(method, identity):
            payload = json.dumps(dict(jsonrpc='2.0', id=identity, method=method)).encode()
            mask = b'abcd'
            stream.write(bytes([0x81, 0x80 | len(payload)]) + mask + bytes(c ^ mask[i % 4] for i, c in enumerate(payload)))

        def receive():
            message = b''
            while True:
                head = stream.read(2)
                length = head[1] & 127
                if length == 126:
                    length = struct.unpack('!H', stream.read(2))[0]
                message += stream.read(length)
                if head[0] & 0x80:
                    return json.loads(message)

        rpc('server.info', 1)
        assert receive()['result']['klippy_state'] == 'ready'
        # Exercise real RFC6455 continuation frames and the subscription snapshot.
        with server.fixture.lock:
            server.fixture.options['fragment_ws'] = True
        rpc('printer.objects.subscribe', 2)
        snapshot = receive()
        assert snapshot['id'] == 2
        assert snapshot['result']['status']['toolhead']['axis_maximum'][:2] == [300, 250]
        post('/__test/control', dict(delta={'extruder': {'target': 0}}))
        assert receive()['params'][0] == {'extruder': {'target': 0}}
        stream.close()
        sock.close()
        print('PASS: local HTTP endpoints, valid/truncated PNG, POST rejection/malformed/uncertain response, command log, WebSocket bootstrap/snapshot/delta/fragmentation')
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=17125)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        server = make_server(args.bind, args.port)
        print(f'Local fixture listening at http://{args.bind}:{args.port}; no printer connection', flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            server.server_close()
