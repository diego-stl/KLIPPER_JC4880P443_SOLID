"""Bounded on-device orientation command check; no flash/NVS writes or synthetic touches."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time
import serial
from esptool.reset import HardReset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--output', type=Path, default=Path('verification/orientation/04-runtime.log'))
    parser.add_argument('--observe-seconds', type=float, default=0)
    parser.add_argument('--reset', action='store_true')
    args = parser.parse_args()
    if not 0 <= args.observe_seconds <= 120:
        parser.error('--observe-seconds must be in 0..120')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    transcript = ''
    checks = {}
    with args.output.open('w', encoding='utf-8') as log, serial.Serial(port=None, baudrate=115200, timeout=.15) as port:
        port.dtr = False
        port.rts = False
        port.port = args.port
        port.open()
        stamp = '# UTC ' + datetime.now(timezone.utc).isoformat() + '\n'
        log.write(stamp)
        print(stamp, end='', flush=True)

        def receive(seconds, until=None):
            nonlocal transcript
            start = len(transcript)
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                raw = port.read(max(1, min(port.in_waiting, 4096)))
                if raw:
                    text = raw.decode('utf-8', errors='replace')
                    transcript += text
                    log.write(text)
                    log.flush()
                    print(text, end='', flush=True)
                    if until and re.search(until, transcript[start:]):
                        return transcript[start:]
            return transcript[start:]

        def command(value, until, seconds=8):
            log.write('\n# SEND ' + value + '\n')
            port.write((value + '\n').encode('ascii'))
            return receive(seconds, until)

        if args.reset:
            HardReset(port, uses_usb=True)()
            boot = receive(25, r'ORIENTATION_TEST_READY')
            checks['boot_ready'] = 'ORIENTATION_TEST_READY' in boot
            checks['console_ready'] = 'ORIENTATION_CONSOLE_READY' in boot
            checks['touch_initialized'] = 'Entrada LVGL activa' in boot
            # Render may complete just after console startup.
            if 'ORIENTATION_FRAME rot=90' not in transcript:
                receive(8, r'ORIENTATION_FRAME rot=90')
            checks['initial_frame'] = 'ORIENTATION_FRAME rot=90' in transcript
        status = command('status', r'TOUCH_COUNTS rot=270[^\r\n]*[\r\n]')
        checks['initial_r90'] = 'ORIENTATION_STATUS rot=90' in status
        mode270 = command('270', r'ORIENTATION_FRAME rot=270')
        checks['r270_presented'] = 'ORIENTATION_FRAME rot=270' in mode270
        status270 = command('status', r'TOUCH_COUNTS rot=270[^\r\n]*[\r\n]')
        checks['r270_active'] = 'ORIENTATION_STATUS rot=270' in status270
        rejected = command('180', r'Comandos:')
        checks['unsupported_rotation_rejected'] = 'Comandos:' in rejected
        repeated = command('270', r'ORIENTATION_REQUEST rot=270')
        checks['repeated_mode_accepted'] = 'ORIENTATION_REQUEST rot=270' in repeated
        mode90 = command('90', r'ORIENTATION_FRAME rot=90')
        checks['r90_presented'] = 'ORIENTATION_FRAME rot=90' in mode90
        checks['no_automatic_count_click'] = not re.search(r'COUNT_CLICK', mode270 + status270 + rejected + repeated + mode90)
        if args.observe_seconds:
            print('\nObservation window open; use the actual touch panel.', flush=True)
            receive(args.observe_seconds)
        final = command('status', r'TOUCH_COUNTS rot=270[^\r\n]*[\r\n]')
        checks['final_status_received'] = 'ORIENTATION_STATUS' in final
        checks['no_runtime_panic'] = not re.search(r'Guru Meditation|assert failed|Timeout de cuadro DMA|Entrada deshabilitada', transcript)
        summary = {
            'utc': datetime.now(timezone.utc).isoformat(),
            'checks': checks,
            'command_checks_pass': all(checks.values()),
            'target_click_events': re.findall(r'TARGET_CLICK[^\r\n]*', transcript),
            'count_click_events': re.findall(r'COUNT_CLICK[^\r\n]*', transcript),
            'physical_visual_acceptance': 'pending_user_observation',
            'selected_mount': None,
            'port_closed_on_exit': True,
        }
        args.output.with_suffix('.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print('\nSerial check complete; ' + args.port + ' closed.', flush=True)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
