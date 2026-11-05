"""Read-only verification of print settings, including an already active job.

No printer POST, setting changes, reset, motion, heating or print start.
The caller must release COM8 before and after this bounded verification.
"""
import argparse
import json
import math
from pathlib import Path
import re
import urllib.parse
import urllib.request
from serial.tools import list_ports
from verify_portal_device import PanelSerial


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='COM8')
    parser.add_argument('--before',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    board=next((p for p in list_ports.comports() if p.device==args.port),None)
    if not board or (board.vid,board.pid,(board.serial_number or '').upper())!=(0x303A,0x1001,'80:F1:B2:D1:7F:0B'):
        raise RuntimeError('Port does not identify the validated panel')
    before=json.loads(args.before.read_text(encoding='utf-8'))['settings']
    checks={}
    with PanelSerial(args.output,args.port) as session:
        session.read(.3)
        session.command('M',r'panel> ',seconds=2)
        network=session.command('status',r'panel> ',seconds=3)
        audio=session.command('audio status',r'panel> ',seconds=3)
        found=re.search(r'Moonraker=(\S+) puerto=(\d+) brillo=(\d+) timeout=(\d+)',network)
        sound=re.search(r'AUDIO_STATUS .*?enabled=(\d).*?volume=(\d+)',audio)
        if not found or not sound:raise RuntimeError('Cannot read preferences')
        settings=dict(host=found[1],port=int(found[2]),brightness=int(found[3]),sleep_minutes=int(found[4]),audio_enabled=int(sound[1]),volume=int(sound[2]))
        checks['preferences_preserved']=settings==before
        session.command('panel files',seconds=3)
        session.command('panel macros',seconds=4)
        session.command('panel metadata',seconds=4)
        status=session.command('panel status',r'PANEL_METADATA[^\r\n]*[\r\n]',seconds=4)
        macros=re.findall(r'(?:^|> )PANEL_MACRO \d+ (.+)\r?$',session.text,re.M)
        checks['public_macros_loaded']=bool(macros) and len(macros)<=20 and all(not m.startswith('_') for m in macros)
        checks['connected']=bool(re.search(r'PANEL_STATUS state=(?:standby|printing|paused|complete|cancelled) available=1 ws=1 snapshot=1',status))
        tuning_observed={}
        tuning_expected={}
        tuning_url=f'http://{settings["host"]}:{settings["port"]}/printer/objects/query?gcode_move=speed_factor,extrude_factor&fan=speed'
        for attempt in range(3):
            values=re.search(r'PANEL_TUNING speed_valid=(\d) speed=([\d.]+) flow_valid=(\d) flow=([\d.]+) fan_valid=(\d) fan=([\d.]+)',status)
            with urllib.request.urlopen(tuning_url,timeout=6) as response:tuning_status=json.load(response)['result']['status']
            matching=values is not None
            for index,(name,object_name,field) in enumerate((('speed','gcode_move','speed_factor'),('flow','gcode_move','extrude_factor'),('fan','fan','speed'))):
                raw=tuning_status.get(object_name,{}).get(field)
                valid=isinstance(raw,(int,float)) and not isinstance(raw,bool) and math.isfinite(raw) and (0<=raw<=1 if name=='fan' else raw>0)
                expected=raw*100 if valid else None
                tuning_expected[name]=expected
                actual=float(values[index*2+2]) if values else None
                observed_valid=bool(int(values[index*2+1])) if values else False
                tuning_observed[name]=dict(valid=observed_valid,percent=actual)
                matching=matching and observed_valid==valid and (not valid or abs(actual-expected)<=0.02)
            if matching:break
            status=session.command('panel status',r'PANEL_METADATA[^\r\n]*[\r\n]',seconds=4)
        checks['real_tuning_readings']=matching
        checks['main_preview_300']=bool(re.search(r'PANEL_METADATA valid=1 .*png=[1-9]\d* 300x300',status))
        file=re.search(r'(?:^|> )PANEL_FILE 0 (.+)\r?$',session.text,re.M)
        if not file:raise RuntimeError('No file returned')
        filename=file[1].strip()
        url=f'http://{settings["host"]}:{settings["port"]}/server/files/metadata?filename='+urllib.parse.quote(filename,safe='')
        with urllib.request.urlopen(url,timeout=6) as response:metadata=json.load(response)['result']
        weights=metadata.get('filament_weights',[])
        referenced=metadata.get('referenced_tools',[])
        expected=sum(w>0 or i in referenced for i,w in enumerate(weights))
        actual=re.search(r'PANEL_METADATA valid=1 .*tools=(\d+) active=(\d+)',status)
        checks['real_filament_normalization']=bool(weights and actual and int(actual[1])==len(weights) and int(actual[2])==expected)
        checks['no_runtime_fault']=not re.search(r'Guru Meditation|assert failed|Brownout detector|CORRUPT HEAP|stack overflow',session.text,re.I)
        session.command('M',r'panel> ',seconds=2)
    result=dict(passed=all(checks.values()),checks=checks,settings=settings,filename=filename,
                filament_weights=weights,expected_active=expected,macros=macros,
                tuning_observed=tuning_observed,tuning_expected=tuning_expected,
                final_status=status,printer_write_requests=0,settings_writes=0,port_closed=True)
    args.output.with_suffix('.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
    return 0 if result['passed'] else 1


if __name__=='__main__':raise SystemExit(main())
