"""Bounded serial verification of the real panel. POST tests require its simulator guard."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time
import serial
from esptool.reset import HardReset


class PanelSerial:
    def __init__(self, output, port='COM8'):
        self.output = Path(output)
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.log = self.output.open('w', encoding='utf-8')
        self.text = ''
        self.pending_output = ''
        self.port = serial.Serial(port=None, baudrate=115200, timeout=.15)
        self.port.dtr = False
        self.port.rts = False
        self.port.port = port

    def __enter__(self):
        self.port.open()
        self.log.write('# UTC ' + datetime.now(timezone.utc).isoformat() + '\n')
        return self

    def __exit__(self, *args):
        self.port.close()
        self.log.close()

    def read(self, seconds, pattern=None):
        start = len(self.text)
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            data = self.port.read(max(1, min(self.port.in_waiting, 8192)))
            if data:
                value = data.decode('utf-8', errors='replace')
                self.text += value
                self.log.write(value)
                self.log.flush()
                # Keep terminal output bounded; the file preserves all diagnostics.
                self.pending_output += value.replace('\r', '')
                complete = self.pending_output.split('\n')
                self.pending_output = complete.pop()
                for line in complete:
                    if any(key in line for key in ('PANEL_', 'PORTAL_', 'GOT_IP', 'COMMAND_RESULT',
                                                   'MOONRAKER_SAVED', 'Guru Meditation', 'assert failed',
                                                   'WIFI_CONSOLE_READY', 'Error', 'ERROR')):
                        print(line, flush=True)
                if pattern and re.search(pattern, self.text[start:]):
                    return self.text[start:]
        return self.text[start:]

    def command(self, command, pattern=None, seconds=6):
        self.log.write('\n# SEND ' + command + '\n')
        self.port.write((command + '\n').encode('utf-8'))
        return self.read(seconds, pattern)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reset', action='store_true')
    parser.add_argument('--host')
    parser.add_argument('--server-port', type=int, default=7125)
    parser.add_argument('--expect', choices=('ready', 'error', 'any'), default='ready')
    parser.add_argument('--test', choices=('normal', 'uncertain'))
    parser.add_argument('--observe-seconds', type=float, default=0)
    args = parser.parse_args()
    checks = {}
    with PanelSerial(args.output, args.port) as session:
        if args.reset:
            HardReset(session.port, uses_usb=True)()
            boot = session.read(75, r'WIFI_CONSOLE_READY')
            checks['application_started'] = 'PORTAL_READY' in boot
            checks['console_ready'] = 'WIFI_CONSOLE_READY' in boot
            checks['gt911_ready'] = 'Entrada LVGL activa' in boot
            checks['psram_ok'] = 'SPI SRAM memory test OK' in boot
        else:
            session.read(.5)
        if args.host:
            saved = session.command(f'moonraker set {args.host} {args.server_port}', r'MOONRAKER_SAVED[^\r\n]*[\r\n]')
            checks['endpoint_saved_by_console'] = f'host={args.host} port={args.server_port}' in saved
        status = ''
        for _ in range(12):
            status = session.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=5)
            ready = re.search(r'PANEL_STATUS state=\w+ available=1 ws=1 snapshot=1', status)
            error = re.search(r'PANEL_STATUS state=error available=0 ws=1', status)
            if (args.expect == 'ready' and ready) or (args.expect == 'error' and error) or args.expect == 'any':
                break
            session.read(1)
        checks['expected_connection_state'] = bool(ready if args.expect == 'ready' else error if args.expect == 'error' else 'PANEL_STATUS' in status)
        if args.test:
            if args.server_port != 17125 or not checks['expected_connection_state']:
                raise RuntimeError('Automatic POST tests require ready simulator port 17125')
            outcome = session.command('panel test ' + args.test, r'PANEL_TEST_(NORMAL|UNCERTAIN)[^\r\n]*[\r\n]', seconds=25)
            checks['simulator_positive_identity'] = 'PANEL_TEST_MOCK_VERIFIED' in outcome
            checks['test_enqueued'] = ('PANEL_TEST_NORMAL' if args.test == 'normal' else 'PANEL_TEST_UNCERTAIN') in outcome
            session.read(5)
        if args.observe_seconds:
            session.read(min(120, args.observe_seconds))
        final = session.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=5)
        checks['no_runtime_panic'] = not re.search(r'Guru Meditation|assert failed|Timeout de cuadro DMA|Entrada deshabilitada', session.text)
        result = {'utc': datetime.now(timezone.utc).isoformat(), 'host': args.host, 'port': args.server_port,
                  'expected': args.expect, 'test': args.test, 'checks': checks,
                  'pass': all(checks.values()), 'final_status': final.replace('\r', '').strip(),
                  'physical_visual_acceptance': 'requires_user_observation', 'port_closed_on_exit': True}
        args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'pass': all(checks.values()), 'checks': checks}), flush=True)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
