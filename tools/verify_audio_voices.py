"""Verify GLaDOS voices on JC4880; simulated printer states, no printer actions."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import sys
import threading
import time
from urllib.request import Request, urlopen

from esptool.reset import HardReset
from serial.tools import list_ports
from verify_audio_device import status, command
from verify_portal_device import PanelSerial

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from klipper_simulator import make_server

CLIPS = ('wakeup', 'printing', 'pause', 'cancel', 'error', 'complete', 'standby')


class VoicesSerial(PanelSerial):
    def read(self, seconds, pattern=None):
        value = super().read(seconds, pattern)
        for line in value.replace('\r', '').splitlines():
            if any(key in line for key in ('AUDIO_CLIP_START', 'AUDIO_CLIP_DONE',
                    'AUDIO_CLIP_ABORT', 'AUDIO_EVENT ', 'AUDIO_READY', 'AUDIO_MUTED')):
                print(line, flush=True)
        return value


def check_board(port):
    board = next((p for p in list_ports.comports() if p.device == port), None)
    if not board or (board.vid, board.pid, (board.serial_number or '').upper()) != (
            0x303A, 0x1001, '80:F1:B2:D1:7F:0B'):
        raise RuntimeError('Port does not identify the previously validated JC4880')


def settings(session):
    text = session.command('status', r'Red guardada[^\r\n]*[\r\n]', 30)
    found = re.search(r'Moonraker=(\S+) puerto=(\d+) brillo=(\d+) timeout=(\d+)', text)
    if not found:
        raise RuntimeError('Cannot preserve the current endpoint/display preferences')
    audio = status(session)
    return {'host': found[1], 'port': int(found[2]), 'brightness': int(found[3]),
            'sleep_minutes': int(found[4]), 'enabled': bool(audio['enabled']),
            'volume': audio['volume']}


def event_status(session):
    text = session.command('audio events', r'AUDIO_EVENTS_CLIP standby=\d+[^\n]*\n', 30)
    found = re.search(r'AUDIO_EVENTS ([^\r\n]+)', text)
    if not found:
        raise RuntimeError('No audio event controller response')
    result = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', found[1])}
    result['clips'] = {k: int(v) for k, v in re.findall(r'AUDIO_EVENTS_CLIP (\w+)=(\d+)', text)}
    return result


def panel_status(session):
    return session.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', 30)


def wait_idle(session, seconds=10):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        now = status(session)
        if not now['ready'] or now['error'] != 'ESP_OK':
            raise RuntimeError('Audio driver failed: ' + str(now))
        if not now['playing'] and not now['queued']:
            return now
        session.read(.15)
    raise RuntimeError('Audio did not finish within the deadline')


def wait_panel(session, state, seconds=20):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        value = panel_status(session)
        if f'PANEL_STATUS state={state} ' in value and 'ws=1' in value:
            return value
        session.read(.3)
    raise RuntimeError('Panel did not reach state ' + state)


def set_endpoint(session, host, port):
    text = session.command(f'moonraker set {host} {port}', r'MOONRAKER_SAVED[^\r\n]*[\r\n]', 30)
    if f'host={host} port={port}' not in text:
        raise RuntimeError('Endpoint was not saved')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--saved-settings', type=Path, required=True)
    parser.add_argument('--fixture-host', default='192.168.0.161')
    parser.add_argument('--prepare-only', action='store_true')
    args = parser.parse_args()
    check_board(args.port)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.prepare_only:
        with VoicesSerial(args.output, args.port) as session:
            current = settings(session)
            args.saved_settings.write_text(json.dumps(current, indent=2) + '\n', encoding='utf-8')
            command(session, 'audio off')
            assert status(session)['enabled'] == 0
        print('Preferences preserved; mute saved for the first upgraded boot.', flush=True)
        return 0

    saved = json.loads(args.saved_settings.read_text(encoding='utf-8'))
    server = make_server(args.fixture_host, 17125)
    server.fixture.update({'job_state': 'printing', 'filename': 'audio-test.gcode'})
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://{args.fixture_host}:17125'

    def control(payload):
        request = Request(base + '/__test/control', data=json.dumps(payload).encode(),
                          headers={'Content-Type': 'application/json'})
        with urlopen(request, timeout=4) as response:
            return json.load(response)

    checks, snapshots = {}, {}
    completed = False
    try:
        with urlopen(base + '/__test/identity', timeout=4) as response:
            assert json.load(response) == {'service': 'JC4880P443_MOONRAKER_SIMULATOR', 'test_only': True}
        with VoicesSerial(args.output, args.port) as session:
            try:
                HardReset(session.port, uses_usb=True)()
                boot = session.read(45, r'WIFI_CONSOLE_READY')
                initial = status(session)
                checks['gui_touch_boot'] = 'PORTAL_READY' in boot and 'Entrada LVGL activa' in boot
                checks['muted_boot_no_voice'] = (initial['ready'] == 1 and initial['enabled'] == 0
                    and initial['volume'] == saved['volume'] and initial['clips_started'] == 0 and initial['queued'] == 0)
                snapshots['muted_boot'] = initial
                command(session, 'audio volume 66')
                command(session, 'audio on')
                session.read(.4)
                checks['unmute_does_not_replay_boot'] = status(session)['clips_started'] == 0
                set_endpoint(session, args.fixture_host, 17125)
                wait_panel(session, 'printing')
                session.read(.4)
                baseline = event_status(session)
                checks['first_printing_snapshot_silent'] = baseline['baseline'] == 1 and baseline['emitted'] == 0

                for clip in CLIPS:
                    before = status(session)
                    reply = session.command('audio play ' + clip, r'AUDIO_PLAY[^\r\n]*[\r\n]', 30)
                    session.read(.12)
                    after = wait_idle(session)
                    checks['manual_' + clip] = ('ESP_OK' in reply
                        and after['clips_completed'] == before['clips_completed'] + 1
                        and after['last_clip'] == clip and after['volume'] == 66)

                # Interrupt the longest clip while it is active and flush two voices behind it.
                session.command('audio play cancel', r'AUDIO_PLAY[^\r\n]*[\r\n]', 30)
                session.read(.2)
                active = status(session)
                checks['voice_active_before_mute'] = active['playing'] == 1
                session.log.write('\n# SEND queued error, standby, then mute\n')
                session.port.write(b'audio play error\naudio play standby\naudio off\n')
                session.read(.5)
                muted = status(session)
                checks['mute_interrupts_voice_and_flushes'] = (muted['enabled'] == 0
                    and muted['playing'] == 0 and muted['queued'] == 0
                    and muted['clips_aborted'] > active['clips_aborted'])
                denied = session.command('audio play wakeup', r'AUDIO_PLAY[^\r\n]*[\r\n]', 30)
                checks['manual_voice_respects_mute'] = 'ESP_ERR_INVALID_STATE' in denied
                command(session, 'audio on')
                session.read(.5)
                checks['unmute_no_pending_voice'] = status(session)['clips_started'] == muted['clips_started']

                # On reboot exactly one wakeup, and no announcement of an already-running job.
                HardReset(session.port, uses_usb=True)()
                session.read(45, r'WIFI_CONSOLE_READY')
                awake = wait_idle(session)
                baseline = event_status(session)
                checks['enabled_boot_one_wakeup_at_66'] = (awake['clips_started'] == 1
                    and awake['clips_completed'] == 1 and awake['last_clip'] == 'wakeup'
                    and awake['volume'] == 66 and baseline['clips']['wakeup'] == 1)
                checks['printing_after_boot_silent'] = baseline['emitted'] == 1 and baseline['state'] == 3
                snapshots['enabled_boot'] = awake

                def transition(job, expected, **extra):
                    before_audio = status(session)
                    before_event = event_status(session)
                    control({'job_state': job, **extra})
                    wait_panel(session, 'error' if extra.get('state') == 'error' else job)
                    session.read(.35)
                    after_audio = wait_idle(session)
                    after_event = event_status(session)
                    checks['transition_' + expected + '_' + str(len(checks))] = (
                        after_event['clips'][expected] == before_event['clips'][expected] + 1
                        and after_audio['clips_completed'] == before_audio['clips_completed'] + 1
                        and after_audio['last_clip'] == expected)

                transition('paused', 'pause')
                transition('printing', 'printing')
                transition('complete', 'complete')
                transition('standby', 'standby')
                transition('printing', 'printing')
                transition('cancelled', 'cancel')
                transition('standby', 'standby')
                transition('printing', 'printing')
                stable = event_status(session)
                control({'disconnect_ws': True})
                session.read(5)
                wait_panel(session, 'printing')
                session.read(.4)
                reconnected = event_status(session)
                checks['reconnection_is_silent'] = (reconnected['reconnect'] > stable['reconnect']
                    and reconnected['emitted'] == stable['emitted'])
                command(session, 'audio off')
                muted_events = event_status(session)
                control({'job_state': 'paused'})
                wait_panel(session, 'paused')
                session.read(.35)
                checks['muted_transition_updates_baseline'] = (
                    event_status(session)['emitted'] == muted_events['emitted']
                    and event_status(session)['state'] == 4)
                command(session, 'audio on')
                session.read(.4)
                checks['unmute_does_not_replay_state'] = event_status(session)['emitted'] == muted_events['emitted']
                transition('printing', 'printing')
                transition('printing', 'error', state='error')
                snapshots['events_before_restore'] = event_status(session)
                checks['no_printer_actions_received'] = not server.fixture.requests
            finally:
                set_endpoint(session, saved['host'], saved['port'])
                command(session, 'audio volume ' + str(saved['volume']))
                command(session, 'audio on' if saved['enabled'] else 'audio off')
                restored = settings(session)
                snapshots['restored_settings'] = restored
                checks['original_settings_restored'] = restored == saved
                session.read(3)
                snapshots['restored_audio'] = status(session)
                snapshots['restored_events'] = event_status(session)
                snapshots['restored_panel'] = panel_status(session)
                checks['no_runtime_faults'] = not re.search(
                    r'Guru Meditation|assert failed|Timeout de cuadro DMA|Entrada deshabilitada|Brownout detector', session.text)
            completed = True
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
        checks['run_completed'] = completed
        result = {'utc': datetime.now(timezone.utc).isoformat(), 'checks': checks,
                  'snapshots': snapshots, 'pass': completed and bool(checks) and all(checks.values()),
                  'fixture_printer_actions': len(server.fixture.requests),
                  'test_volume': 66, 'restore_target': saved,
                  'restored_settings': snapshots.get('restored_settings'),
                  'real_printer_actions_sent': False, 'audible_acceptance': 'requires_user_observation'}
        args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2), flush=True)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
