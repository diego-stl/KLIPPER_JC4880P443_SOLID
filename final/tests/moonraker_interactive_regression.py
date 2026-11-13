"""State, HTTP/WebSocket and real console entry-point regressions; no hardware."""
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener

from klipper_simulator import Fixture, make_server
from moonraker_model import FILES

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from moonraker_console import Client


class ModelTests(unittest.TestCase):
    def setUp(self):
        self.fixture = Fixture(interactive=True)
        self.model = self.fixture.simulation
        self.options = self.fixture.options

    def test_pause_preserves_time_and_resume_completes(self):
        self.model.action('start', dict(filename=FILES[0]['path']))
        self.model.advance(20)
        self.model.gcode('PAUSE')
        self.model.advance(100)
        self.assertAlmostEqual(self.options['print_duration'], 20)
        self.assertAlmostEqual(self.options['progress'], 1 / 3)
        self.model.gcode('RESUME')
        self.model.advance(40)
        self.assertEqual(self.options['job_state'], 'complete')
        self.assertAlmostEqual(self.options['print_duration'], 60)

    def test_three_colors_require_two_manual_resumes(self):
        self.model.action('start', dict(filename=FILES[1]['path']))
        for color in range(2):
            self.model.advance(30)
            self.assertEqual(self.options['job_state'], 'paused')
            self.assertTrue(self.options['color_change_pending'])
            self.model.advance(99)
            self.assertAlmostEqual(self.options['print_duration'], (color + 1) * 30)
            self.model.gcode('RESUME')
            self.assertEqual(self.options['color_index'], color + 1)
        self.model.advance(30)
        self.assertEqual(self.options['job_state'], 'complete')
        self.assertEqual(self.options['color_changes_done'], 2)
        self.assertAlmostEqual(self.options['print_duration'], 90)

    def test_cancel_and_reset_cannot_continue_old_print(self):
        self.model.gcode(f'SDCARD_PRINT_FILE FILENAME="{FILES[2]["path"]}"')
        self.model.advance(10)
        self.model.gcode('CANCEL_PRINT')
        progress = self.options['progress']
        self.model.advance(100)
        self.assertEqual(self.options['job_state'], 'cancelled')
        self.assertEqual(self.options['progress'], progress)
        self.model.gcode('SDCARD_RESET_FILE')
        self.assertEqual(self.options['filename'], '')
        self.assertEqual(self.options['job_state'], 'standby')

    def test_temperature_tuning_and_relative_moves(self):
        self.model.gcode('M104 S180\nM140 S55\nM220 S200\nM221 S95\nM106 S128\nG28')
        self.model.advance(10)
        self.assertEqual(self.options['extruder_temperature'], 105)
        self.assertEqual(self.options['bed_temperature'], 55)
        self.assertEqual(self.options['speed_factor'], 2)
        self.assertEqual(self.options['extrude_factor'], .95)
        self.assertAlmostEqual(self.options['fan_speed'], 128 / 255)
        self.model.gcode('SAVE_GCODE_STATE NAME=MOVE\nG91\nG1 X10 Y5\nRESTORE_GCODE_STATE NAME=MOVE MOVE=0\nG1 X20')
        self.assertEqual(self.options['position'][:2], [20, 5])
        self.model.gcode('M104 S0\nM140 S0\nM107\nM84')
        self.assertEqual(self.options['extruder_target'], 0)
        self.assertEqual(self.options['homed_axes'], '')

    def test_manual_mode_and_speed_affect_clock(self):
        self.model.action('start', {})
        self.model.action('auto', dict(enabled=False))
        self.model.advance(20)
        self.assertEqual(self.options['progress'], 0)
        self.model.action('auto', dict(enabled=True))
        self.model.gcode('M220 S200')
        self.model.advance(30)
        self.assertEqual(self.options['job_state'], 'complete')
        self.assertAlmostEqual(self.options['print_duration'], 30)

    def test_emergency_recovery_and_invalid_values(self):
        self.model.action('start', {})
        self.model.action('emergency', {})
        self.assertEqual(self.options['state'], 'shutdown')
        self.assertEqual(self.options['extruder_target'], 0)
        self.model.gcode('FIRMWARE_RESTART')
        self.assertEqual(self.options['state'], 'ready')
        self.assertEqual(self.options['job_state'], 'standby')
        for payload in (dict(hotend=float('nan')), dict(hotend=301), dict(bed=-1)):
            with self.assertRaises(ValueError):
                self.model.action('temperatures', payload)
        with self.assertRaises(ValueError):
            self.model.action('start', dict(seconds=0))
        self.assertEqual(self.options['job_state'], 'standby')


class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = make_server(port=0, interactive=True)
        cls.worker = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.worker.start()
        cls.base = f'http://127.0.0.1:{cls.server.server_port}'
        cls.http = build_opener(ProxyHandler({}))

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.worker.join(timeout=2)
        assert not cls.server.clock_thread.is_alive()

    def api(self, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        with self.http.open(Request(self.base + path, data=data, headers={'Content-Type': 'application/json'}), timeout=3) as response:
            return json.load(response)

    def setUp(self):
        self.server.fixture.act('standby', {})
        self.server.fixture.act('clear_faults', {})
        with self.server.fixture.lock:
            self.server.fixture.requests.clear()

    def cli(self, *args, input=None, wrapper=None):
        executable = wrapper or [sys.executable, str(ROOT / 'tools/moonraker_console.py')]
        return subprocess.run(executable + ['--url', self.base, *args], input=input, capture_output=True,
                              text=True, encoding='utf-8', errors='replace', timeout=10, cwd=ROOT)

    def test_catalog_metadata_thumbnails_and_gcode(self):
        files = self.api('/server/files/list?root=gcodes')['result']
        self.assertEqual([entry['path'] for entry in files], [entry['path'] for entry in FILES])
        self.assertEqual(len(files), 4)
        for entry in FILES:
            meta = self.api('/server/files/metadata?filename=' + entry['path'])['result']
            self.assertEqual(meta['estimated_time'], entry['seconds'])
            self.assertEqual(meta['filament_colors'], entry['colors'])
            for thumbnail in meta['thumbnails']:
                with self.http.open(self.base + '/server/files/gcodes/' + thumbnail['relative_path']) as response:
                    image = response.read()
                self.assertEqual(image[:8], b'\x89PNG\r\n\x1a\n')
                self.assertEqual(struct.unpack('!II', image[16:24]), (thumbnail['width'], thumbnail['height']))
            with self.http.open(self.base + '/server/files/gcodes/' + entry['path']) as response:
                self.assertIn(b'ARCHIVO VIRTUAL', response.read())

    def test_screen_commands_publish_websocket_states(self):
        with socket.create_connection(('127.0.0.1', self.server.server_port), timeout=3) as sock:
            stream = sock.makefile('rwb', buffering=0)
            stream.write(b'GET /websocket HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n')
            self.assertIn(b'101', stream.readline())
            while stream.readline() != b'\r\n':
                pass
            def receive():
                header = stream.read(2)
                size = header[1] & 127
                if size == 126:
                    size = struct.unpack('!H', stream.read(2))[0]
                return json.loads(stream.read(size))
            request = json.dumps(dict(jsonrpc='2.0', id=1, method='printer.objects.subscribe')).encode()
            mask = b'test'
            stream.write(bytes([0x81, 0x80 | len(request)]) + mask + bytes(value ^ mask[i % 4] for i, value in enumerate(request)))
            while receive().get('id') != 1:
                pass
            for script, expected in ((f'SDCARD_PRINT_FILE FILENAME="{FILES[0]["path"]}"', 'printing'),
                                     ('PAUSE', 'paused'), ('RESUME', 'printing'), ('CANCEL_PRINT', 'cancelled')):
                self.api('/printer/gcode/script', dict(script=script))
                for _ in range(20):
                    message = receive()
                    if message.get('method') == 'notify_status_update' and message['params'][0]['print_stats']['state'] == expected:
                        break
                else:
                    self.fail('No se publico el estado ' + expected)
            stream.close()
        self.assertEqual(len(self.api('/__test/requests')), 4)

    def test_real_clock_completes_short_job(self):
        self.api('/__test/action', dict(action='start', seconds=1))
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            status = self.api('/__test/status')['status']
            if status['print_stats']['state'] == 'complete':
                break
            time.sleep(.1)
        self.assertEqual(status['print_stats']['state'], 'complete')
        self.assertEqual(status['virtual_sdcard']['progress'], 1)

    def test_menu_and_cli_actions(self):
        result = self.cli(input='1\n1\n\n2\n0\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('OPCIONES', result.stdout)
        self.assertIn('EN PAUSA', result.stdout)
        self.assertEqual(self.api('/__test/status')['status']['print_stats']['state'], 'paused')
        result = self.cli('resume')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.api('/__test/status')['status']['print_stats']['state'], 'printing')
        result = self.cli('tuning', '--speed', '120', '--flow', '95', '--fan', '50')
        self.assertEqual(result.returncode, 0, result.stderr)
        result = self.cli('--json', 'status')
        self.assertEqual(json.loads(result.stdout)['status']['gcode_move']['speed_factor'], 1.2)

    def test_faults_reject_without_execution_and_drop_after_execution(self):
        self.api('/__test/action', dict(action='fault', mode='reject'))
        with self.assertRaises(HTTPError):
            self.api('/printer/gcode/script', dict(script='M104 S180'))
        self.assertEqual(self.api('/__test/status')['status']['extruder']['target'], 0)
        self.api('/__test/action', dict(action='clear_faults'))
        self.api('/__test/action', dict(action='fault', mode='drop'))
        from http.client import RemoteDisconnected
        with self.assertRaises(RemoteDisconnected):
            self.api('/printer/gcode/script', dict(script='M104 S180'))
        self.assertEqual(self.api('/__test/status')['status']['extruder']['target'], 180)

    def test_legacy_fixture_is_not_mutated_by_console(self):
        server = make_server(port=0)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            with self.assertRaisesRegex(ValueError, 'fixture antiguo'):
                Client(f'http://127.0.0.1:{server.server_port}')
            self.assertEqual(server.fixture.requests, [])
        finally:
            server.shutdown()
            server.server_close()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows wrappers')
    def test_cmd_and_bash_entrypoints(self):
        result = self.cli('--json', 'files', wrapper=['cmd.exe', '/d', '/c', str(ROOT / 'tools/moonraker_mock.cmd')])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(json.loads(result.stdout)['files']), 4)
        bash = Path(r'D:\Program Files\Git\bin\bash.exe')
        if bash.exists():
            result = self.cli('--json', 'status', wrapper=[str(bash), str(ROOT / 'tools/moonraker_mock.sh')])
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(json.loads(result.stdout)['interactive'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
