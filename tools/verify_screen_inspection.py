"""Verify screen inspection over USB; read-only queries, no printer/settings writes."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re

from esptool.reset import HardReset
from serial.tools import list_ports
from verify_portal_device import PanelSerial

VIEWS = ('startup', 'standby', 'printing', 'paused', 'toolhead', 'files',
         'macros', 'exclude', 'settings', 'error', 'details')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--reset', action='store_true')
    parser.add_argument('--expected-version', default='0.6.0-portal-controls')
    args = parser.parse_args()
    board = next((p for p in list_ports.comports() if p.device == args.port), None)
    if not board or (board.vid, board.pid, (board.serial_number or '').upper()) != (
            0x303A, 0x1001, '80:F1:B2:D1:7F:0B'):
        raise RuntimeError('Serial port does not identify the validated JC4880')
    checks, observed = {}, {}
    failure = None
    with PanelSerial(args.output, args.port) as session:
        def check(name, value):
            checks[name] = bool(value)
            print(('PASS ' if value else 'FAIL ') + name, flush=True)
            if not value:
                raise RuntimeError(name)

        def command(line, prompt='inspeccion> '):
            text = session.command(line, re.escape(prompt), seconds=6)
            if prompt not in text:
                raise RuntimeError('Missing prompt after ' + repr(line))
            return text

        def screen_status(prompt='inspeccion> '):
            for _ in range(8):
                text = command('screen status', prompt)
                found = re.search(r'SCREEN_STATUS active=(\d) view=(\w+) pending=(\d) requested=(\d+) applied=(\d+)', text)
                if not found:
                    raise RuntimeError('No SCREEN_STATUS response')
                status = dict(zip(('active', 'view', 'pending', 'requested', 'applied'), found.groups()))
                if status['pending'] == '0':
                    return status
                session.read(.25)
            raise RuntimeError('GUI did not apply the screen request')

        def settings(prompt):
            network = command('status', prompt)
            audio = command('audio status', prompt)
            endpoint = re.search(r'Moonraker=(\S+) puerto=(\d+) brillo=(\d+) timeout=(\d+)', network)
            sound = re.search(r'AUDIO_STATUS .*?enabled=(\d).*?volume=(\d+)', audio)
            if not endpoint or not sound:
                raise RuntimeError('Cannot read persisted settings')
            return {'host': endpoint[1], 'port': int(endpoint[2]), 'brightness': int(endpoint[3]),
                    'sleep_minutes': int(endpoint[4]), 'audio_enabled': int(sound[1]), 'volume': int(sound[2])}

        def voices(prompt):
            text = command('audio events', prompt)
            return dict(re.findall(r'AUDIO_EVENTS_CLIP (\w+)=(\d+)', text))

        try:
            if args.reset:
                HardReset(session.port, uses_usb=True)()
                boot = session.read(55, r'panel> ')
                check('version_loaded', args.expected_version in boot)
                check('portal_started', 'PORTAL_READY' in boot)
                check('console_ready', 'WIFI_CONSOLE_READY' in boot)
            else:
                session.read(.5)
            command('M', 'panel> ')
            observed['before'] = settings('panel> ')
            before_voices = voices('panel> ')
            check('seven_voice_counters', len(before_voices) == 7)
            observed['klipper_before'] = command('panel status', 'panel> ')
            command('P', 'pantallas> ')
            menu = command('4')
            check('inspection_menu', 'INSPECCION DE PANTALLAS' in menu)
            for number, view in enumerate(VIEWS, 1):
                response = command(str(number))
                check('request_' + view, 'SCREEN_REQUEST' in response)
                session.read(.35)
                status = screen_status()
                check('applied_' + view, status['active'] == '1' and status['view'] == view)
            command('3')
            session.read(.75)
            status = screen_status()
            check('printing_view_held', status['active'] == '1' and status['view'] == 'printing')
            before_invalid = status['requested']
            response = command('screen show invalid')
            check('invalid_rejected', 'SCREEN_ERROR vista invalida' in response)
            status = screen_status()
            check('invalid_does_not_change_view', status['requested'] == before_invalid and status['view'] == 'printing')
            response = command('13')
            check('numbered_status', 'SCREEN_STATUS' in response)
            command('0', 'pantallas> ')
            status = screen_status('pantallas> ')
            check('zero_exits_inspection', status['active'] == '0')
            command('screen show settings')
            status = screen_status()
            check('full_command_opens_inspection', status['active'] == '1' and status['view'] == 'settings')
            command('M', 'panel> ')
            status = screen_status('panel> ')
            check('main_exits_inspection', status['active'] == '0')
            observed['after'] = settings('panel> ')
            check('settings_unchanged', observed['before'] == observed['after'])
            after_voices = voices('panel> ')
            observed['voice_counters_before'] = before_voices
            observed['voice_counters_after'] = after_voices
            check('no_extra_voices', before_voices == after_voices)
            panel = command('panel status', 'panel> ')
            observed['klipper_after'] = panel
            check('klipper_connected', bool(re.search(r'available=1 ws=1 snapshot=1', panel)))
            check('no_runtime_fault', not re.search(
                r'Guru Meditation|assert failed|Brownout detector|CORRUPT HEAP|stack overflow', session.text, re.I))
        except Exception as error:
            failure = str(error)
            checks['verification_completed'] = False
        finally:
            # Always release the inspection shield and leave the console usable.
            session.command('screen live', seconds=.7)
            session.command('M', seconds=.7)
    result = {'utc': datetime.now(timezone.utc).isoformat(), 'checks': checks,
              'observed': observed, 'passed': not failure and all(checks.values()),
              'error': failure, 'port_closed': True, 'printer_actions_sent': 0,
              'settings_writes_sent': 0}
    args.output.with_suffix('.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
