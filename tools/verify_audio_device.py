"""Explicit 50% tone and mute/queue/persistence verification. No printer actions."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re

from esptool.reset import HardReset
from serial.tools import list_ports
from verify_portal_device import PanelSerial


class AudioSerial(PanelSerial):
    def read(self, seconds, pattern=None):
        value = super().read(seconds, pattern)
        for line in value.replace('\r', '').splitlines():
            if 'AUDIO_' in line:
                print(line, flush=True)
        return value


def status(session, seconds=30):
    # The existing console also owns Wi-Fi reconnect attempts; allow them to finish.
    value = session.command('audio status', r'AUDIO_STATUS[^\r\n]*[\r\n]', seconds)
    match = re.search(r'AUDIO_STATUS ([^\r\n]*)', value)
    if not match:
        raise RuntimeError('No audio status response')
    fields = dict(re.findall(r'(\w+)=(\S+)', match[1]))
    return {key: int(value) if value.isdecimal() else value for key, value in fields.items()}


def command(session, text):
    value = session.command(text, r'AUDIO_(COMMAND|TEST)[^\r\n]*[\r\n]', 30)
    if text in ('audio on', 'audio off') or text.startswith('audio volume '):
        for _ in range(15):
            current = status(session)
            if not current.get('pending', 0):
                break
            session.read(.1)
        else:
            raise RuntimeError('Audio settings were not saved before the deadline')
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    board = next((p for p in list_ports.comports() if p.device == args.port), None)
    if not board or (board.vid, board.pid, (board.serial_number or '').upper()) != (
            0x303A, 0x1001, '80:F1:B2:D1:7F:0B'):
        raise RuntimeError('Refusing a port other than the previously verified JC4880')
    checks, observations = {}, {}
    with AudioSerial(args.output, args.port) as session:
        HardReset(session.port, uses_usb=True)()
        boot = session.read(40, r'WIFI_CONSOLE_READY')
        checks['boot_gui_touch'] = 'PORTAL_READY' in boot and 'Entrada LVGL activa' in boot
        before = status(session)
        checks['codec_ready'] = before['ready'] == 1 and before['error'] == 'ESP_OK'
        checks['silent_boot'] = before['started'] == 0 and before['queued'] == 0
        observations['initial'] = before
        if not checks['codec_ready']:
            raise RuntimeError('Codec failed initialization; no tone requested')
        command(session, 'audio volume 50')
        command(session, 'audio on')
        session.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', 4)
        print('Running the explicit 1 kHz / 500 ms tone at volume 50%.', flush=True)
        command(session, 'audio test')
        session.read(.8)
        tone = status(session)
        checks['medium_volume_tone_completed'] = (
            tone['volume'] == 50 and tone['completed'] == before['completed'] + 1
            and tone['playing'] == 0 and tone['error'] == 'ESP_OK')
        observations['tone'] = tone

        # Confirm an active tone before mute, then enqueue two more and mute.
        command(session, 'audio test')
        session.read(.08)
        active = status(session)
        for _ in range(3):
            if active['playing'] == 1:
                break
            session.read(.05)
            active = status(session)
        checks['observed_active_tone'] = active['playing'] == 1
        session.log.write('\n# SEND audio test, audio test, audio off (batch)\n')
        session.port.write(b'audio test\naudio test\naudio off\n')
        session.read(1)
        muted = status(session)
        checks['mute_aborts_and_flushes'] = (
            muted['enabled'] == 0 and muted['queued'] == 0 and muted['playing'] == 0
            and muted['aborted'] > tone['aborted'])
        rejected = command(session, 'audio test')
        checks['explicit_test_respects_mute'] = 'ESP_ERR_INVALID_STATE' in rejected
        command(session, 'audio on')
        session.read(.7)
        resumed = status(session)
        checks['unmute_does_not_resume_queue'] = (
            resumed['started'] == muted['started'] and resumed['queued'] == 0
            and resumed['playing'] == 0)
        observations['muted'] = muted
        observations['unmuted'] = resumed

        command(session, 'audio off')
        HardReset(session.port, uses_usb=True)()
        second_boot = session.read(40, r'WIFI_CONSOLE_READY')
        restored = status(session)
        checks['mute_and_volume_persist'] = restored['enabled'] == 0 and restored['volume'] == 50
        checks['muted_reset_stays_silent'] = (
            restored['ready'] == 1 and restored['started'] == 0
            and restored['playing'] == 0 and restored['queued'] == 0)
        checks['gui_recovers_after_reset'] = 'PORTAL_READY' in second_boot
        observations['after_reset'] = restored
        command(session, 'audio on')
        final = status(session)
        checks['final_manual_test_ready_at_50'] = (
            final['enabled'] == 1 and final['ready'] == 1 and final['volume'] == 50
            and final['started'] == 0 and final['queued'] == 0 and final['error'] == 'ESP_OK')
        session.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', 4)
        session.read(2)
        checks['no_panic_or_display_touch_fault'] = not re.search(
            r'Guru Meditation|assert failed|Timeout de cuadro DMA|Entrada deshabilitada', session.text)
        observations['final'] = final
    report = {'utc': datetime.now(timezone.utc).isoformat(), 'checks': checks,
              'observations': observations, 'pass': all(checks.values()),
              'audible_result': 'requires_user_observation', 'port_closed_on_exit': True,
              'printer_actions_sent': False}
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)
    return 0 if report['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
