"""Real ESP32 transport verification against the identity-guarded local fixture only."""
import json
from pathlib import Path
import re
import time
import urllib.request
from verify_portal_device import PanelSerial

BASE = 'http://127.0.0.1:17125'
OUT = Path('verification/portal')


def api(path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    request = urllib.request.Request(BASE + path, data=data, headers={'Content-Type': 'application/json'})
    return json.load(urllib.request.urlopen(request, timeout=4))


def main():
    assert api('/__test/identity') == {'service': 'JC4880P443_MOONRAKER_SIMULATOR', 'test_only': True}
    OUT.mkdir(parents=True, exist_ok=True)
    checks = {}
    normal = api('/__test/requests')
    scripts = [x.get('body', {}).get('script', '') for x in normal]
    checks['normal_commands_received'] = all(x in scripts for x in ('G28', 'M104 S170', 'M140 S55', 'PAUSE', 'RESUME', 'TEST_0'))
    checks['print_received'] = any(x.startswith('SDCARD_PRINT_FILE ') for x in scripts)
    checks['exclusion_received'] = any(x.startswith('EXCLUDE_OBJECT ') for x in scripts)
    checks['emergency_received'] = any(x['path'] == '/printer/emergency_stop' for x in normal)
    checks['host_shutdown_received'] = any(x['path'] == '/machine/shutdown' for x in normal)
    (OUT / '11-mock-normal-requests.json').write_text(json.dumps(normal, indent=2) + '\n')
    with PanelSerial(OUT / '11-mock-resilience.log') as serial:
        api('/__test/control', {'delay_post': 1, 'drop_post': True})
        try:
            begin = len(normal)
            outcome = serial.command('panel test uncertain', r'PANEL_TEST_UNCERTAIN[^\r\n]*[\r\n]', seconds=10)
            checks['uncertain_guard'] = 'PANEL_TEST_MOCK_VERIFIED' in outcome
            records = serial.read(5)
            after = api('/__test/requests')[begin:]
            checks['uncertain_first_received_once'] = sum(x['body'].get('script') == 'M118 PANEL_TEST_UNCERTAIN_FIRST' for x in after) == 1
            checks['uncertain_second_discarded'] = not any(x['body'].get('script') == 'M118 PANEL_TEST_UNCERTAIN_SECOND' for x in after)
            checks['uncertain_result_published'] = bool(re.search(r'COMMAND_RESULT id=\d+ status=3', records))
            checks['remaining_batch_rejected'] = bool(re.search(r'COMMAND_RESULT id=\d+ status=2', records))
        finally:
            api('/__test/control', {'delay_post': 0, 'drop_post': False})
        api('/__test/control', {'fragment_ws': True, 'job_state': 'printing',
                                'filename': 'folder/part 24.gcode', 'progress': .42,
                                'disconnect_ws': True})
        serial.read(7)
        status = ''
        for _ in range(8):
            status = serial.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=4)
            if 'state=printing available=1 ws=1 snapshot=1' in status and 'progress=42.0%' in status:
                break
            serial.read(1)
        checks['fragmented_snapshot_recovered'] = 'state=printing available=1 ws=1 snapshot=1' in status
        checks['progress_correct'] = 'progress=42.0%' in status
        api('/__test/control', {'delta': {'extruder': {'target': 0}, 'print_stats': {'state': 'paused'}}})
        serial.read(1)
        paused = serial.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=4)
        checks['delta_zero_preserved'] = 'hotend=205.0/0.0' in paused
        checks['pause_delta_applied'] = 'state=paused available=1' in paused
        serial.command('panel metadata', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=10)
        # A malformed/truncated download must leave no thumbnail, then recover.
        api('/__test/control', {'truncated_png': True})
        bad = serial.command('panel metadata', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=10)
        checks['truncated_png_not_published'] = 'png=0 0x0' in bad
        api('/__test/control', {'truncated_png': False})
        valid = serial.command('panel metadata', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=10)
        checks['thumbnail_recovers'] = bool(re.search(r'png=[1-9]\d* 100x100', valid))
        reconnect = serial.command('reconnect-test', r'RECONNECT_TEST_END ip=1', seconds=40)
        checks['wifi_reconnect'] = 'RECONNECT_TEST_END ip=1' in reconnect
        serial.read(7)
        recovered = serial.command('panel status', r'PANEL_METADATA[^\r\n]*[\r\n]', seconds=6)
        checks['websocket_after_wifi_reconnect'] = 'available=1 ws=1 snapshot=1' in recovered
        checks['no_runtime_panic'] = not re.search(r'Guru Meditation|assert failed|Timeout de cuadro DMA|Entrada deshabilitada', serial.text)
    result = {'checks': checks, 'pass': all(checks.values()), 'normal_requests': len(normal),
              'real_printer_contacted_by_test': False,
              'note': 'All POST routes target the positively identified local simulator, never the real printer.'}
    (OUT / '11-mock-resilience.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2), flush=True)
    return 0 if all(checks.values()) else 1


if __name__ == '__main__':
    raise SystemExit(main())
