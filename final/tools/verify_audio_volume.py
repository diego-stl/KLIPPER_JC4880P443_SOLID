"""Check boot and the longest original voice at an explicitly chosen volume."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re

from esptool.reset import HardReset
from verify_audio_device import command, status
from verify_audio_voices import VoicesSerial, check_board, settings, panel_status, wait_idle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM8')
    parser.add_argument('--volume', type=int, required=True, choices=range(1, 101))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    check_board(args.port)
    checks, observations = {}, {}
    completed = False
    try:
        with VoicesSerial(args.output, args.port) as session:
            command(session, 'audio volume ' + str(args.volume))
            command(session, 'audio on')
            initial = status(session)
            checks['volume_saved'] = initial['volume'] == args.volume and initial['pending'] == 0
            HardReset(session.port, uses_usb=True)()
            session.read(45, r'WIFI_CONSOLE_READY')
            boot = wait_idle(session)
            checks['boot_voice_completed'] = (boot['volume'] == args.volume
                and boot['clips_started'] == 1 and boot['clips_completed'] == 1
                and boot['last_clip'] == 'wakeup' and boot['error'] == 'ESP_OK')
            session.command('audio play cancel', r'AUDIO_PLAY[^\r\n]*[\r\n]', 30)
            session.read(.2)
            long_voice = wait_idle(session)
            checks['long_voice_completed'] = (long_voice['clips_completed'] == boot['clips_completed'] + 1
                and long_voice['last_clip'] == 'cancel' and long_voice['volume'] == args.volume)
            preferences = settings(session)
            panel = panel_status(session)
            checks['real_endpoint_preserved'] = preferences['host'] == '192.168.0.235' and preferences['port'] == 7125
            checks['real_printer_connected'] = 'available=1 ws=1 snapshot=1' in panel
            checks['no_runtime_faults'] = not re.search(
                r'Brownout detector|Guru Meditation|assert failed|Timeout de cuadro DMA|Entrada deshabilitada', session.text)
            checks['idle_with_empty_queue'] = not long_voice['playing'] and not long_voice['queued']
            observations = {'boot': boot, 'long_voice': long_voice, 'settings': preferences, 'panel': panel}
            completed = True
    finally:
        checks['run_completed'] = completed
        result = {'utc': datetime.now(timezone.utc).isoformat(), 'volume': args.volume,
                  'checks': checks, 'observations': observations,
                  'pass': completed and all(checks.values()), 'printer_actions_sent': False}
        args.output.with_suffix('.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(result, indent=2), flush=True)
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
