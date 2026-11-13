"""Console menu and scriptable CLI for the positively identified test server."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener


DEFAULT_URL = 'http://127.0.0.1:17125'


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, new_url):
        return None


class Client:
    def __init__(self, url):
        parsed = urlsplit(url)
        if parsed.scheme != 'http' or not parsed.hostname or parsed.path not in ('', '/') or parsed.query or parsed.fragment or parsed.username:
            raise ValueError('Usa una URL http://IP:PUERTO sin rutas ni credenciales')
        self.base = url.rstrip('/')
        self.http = build_opener(ProxyHandler({}), NoRedirect())
        if self.api('/__test/identity') != dict(service='JC4880P443_MOONRAKER_SIMULATOR', test_only=True):
            raise ValueError('El destino no se identifica como el simulador de pruebas')
        if not self.api('/__test/status').get('interactive'):
            raise ValueError('El servidor usa el fixture antiguo. Reinicialo con --interactive')

    def api(self, path, body=None):
        data = None if body is None else json.dumps(body).encode()
        request = Request(self.base + path, data=data, headers={'Content-Type': 'application/json'})
        try:
            with self.http.open(request, timeout=5) as response:
                return json.load(response)
        except HTTPError as error:
            try:
                message = json.load(error)['error']['message']
            except (ValueError, KeyError, TypeError):
                message = f'HTTP {error.code}'
            raise ValueError(message) from None

    def action(self, action, **payload):
        return self.api('/__test/action', dict(action=action, **payload))


def status_text(status):
    state = status['status']
    job = state['print_stats']
    sim = status['simulation']
    progress = state['virtual_sdcard']['progress'] * 100
    labels = dict(standby='EN ESPERA', printing='IMPRIMIENDO', paused='EN PAUSA',
                  complete='COMPLETADA', cancelled='CANCELADA', error='ERROR')
    lines = [f"Klipper: {state['webhooks']['state']} | Trabajo: {labels.get(job['state'], job['state'])}",
             f"Archivo: {job['filename'] or '(ninguno)'}",
             f"Progreso: {progress:.1f}% | Tiempo activo: {job['print_duration']:.1f}s | Duracion base: {sim['duration_seconds']:g}s",
             f"Avance automatico: {'SI' if sim['auto_progress'] else 'NO'} | WebSocket: {status['websocket_clients']} | Ordenes recibidas: {status['received_commands']}"]
    for label, key in (('Boquilla', 'extruder'), ('Cama', 'heater_bed')):
        lines.append(f"{label}: {state[key]['temperature']:.1f} / {state[key]['target']:g} C")
    tuning = state['gcode_move']
    lines.append(f"Velocidad: {tuning['speed_factor'] * 100:.0f}% | Flujo: {tuning['extrude_factor'] * 100:.0f}% | Ventilador: {state['fan']['speed'] * 100:.0f}%")
    lines.append(f"Ejes referenciados: {state['toolhead']['homed_axes'] or '(ninguno)'} | Posicion: {state['toolhead']['position']}")
    if sim['colors']:
        lines.append(f"Color: {sim['color_index'] + 1}/{len(sim['colors'])} {sim['colors'][sim['color_index']]} | Cambios confirmados: {sim['color_changes_done']}")
    if sim['pause_reason']:
        lines.append('Motivo: ' + sim['pause_reason'])
    if state['webhooks']['state'] != 'ready':
        lines.append('Detalle: ' + state['webhooks']['state_message'])
    faults = [f'{key}={value}' for key, value in status['faults'].items() if value]
    lines.append('Fallos activos: ' + (', '.join(faults) if faults else 'ninguno'))
    if status['peers']:
        lines.append('Clientes: ' + ', '.join(peer['ip'] for peer in status['peers']))
    return '\n'.join(lines)


def print_files(files):
    print('\nARCHIVOS VIRTUALES')
    for index, entry in enumerate(files, 1):
        changes = ' | cambios a ' + ', '.join(f'{second}s' for second in entry['changes']) if entry['changes'] else ''
        print(f"  {index}. {entry['path']} | {entry['seconds']}s | {len(entry['colors'])} color(es){changes}")
        print('     ' + entry['description'])


def print_history(client, limit=20):
    records = client.api('/__test/requests')
    print(f'\nORDENES RECIBIDAS: {len(records)}')
    for record in records[-limit:]:
        print(f"{record.get('received_at', '')} [{record.get('source', '')}] {record['path']} {json.dumps(record['body'], ensure_ascii=False)}")


def watch(client, interval=1):
    try:
        while True:
            if sys.stdout.isatty():
                print('\033[2J\033[H', end='')
            print(status_text(client.api('/__test/status')))
            print('\nCtrl+C para salir del monitor. El servidor continua activo.')
            time.sleep(interval)
    except KeyboardInterrupt:
        print()


def prompt_values(fields):
    values = {}
    for key, label in fields:
        value = input(label + ' (Enter = conservar): ').strip()
        if value:
            values[key] = float(value)
    return values


def menu(client):
    while True:
        print('\n' + '=' * 64)
        print('MOONRAKER INTERACTIVO DE PRUEBAS | ' + client.base)
        print(status_text(client.api('/__test/status')))
        actions = client.api('/__test/actions')['actions']
        print('\nOPCIONES')
        for index, action in enumerate(actions, 1):
            print(f" {index:2}. {action['label']}" + ('' if action['enabled'] else ' [no disponible]'))
        print('  f. Ver archivos | v. Monitor en vivo | Enter. Actualizar | 0. Salir')
        choice = input('\nOpcion: ').strip().lower()
        if choice in ('0', 'q', 'salir'):
            print('Menu cerrado. El servidor sigue activo para la pantalla.')
            return
        try:
            if not choice:
                continue
            if choice == 'v':
                watch(client)
                continue
            if choice == 'f':
                print_files(client.api('/__test/files')['files'])
                input('\nEnter para volver...')
                continue
            index = int(choice) - 1
            if not 0 <= index < len(actions):
                raise ValueError('Opcion desconocida')
            selected = actions[index]
            if not selected['enabled']:
                raise ValueError('Esa opcion no esta disponible en el estado actual')
            action = selected['id']
            payload = {}
            if action == 'history':
                print_history(client)
                input('\nEnter para volver...')
                continue
            if action == 'start':
                files = client.api('/__test/files')['files']
                print_files(files)
                index = int(input('\nNumero de archivo [1]: ').strip() or '1') - 1
                if not 0 <= index < len(files):
                    raise ValueError('Archivo desconocido')
                payload['filename'] = files[index]['path']
                seconds = input(f"Duracion en segundos [{files[index]['seconds']}]: ").strip()
                if seconds:
                    payload['seconds'] = float(seconds)
            elif action == 'temperatures':
                payload = prompt_values((('hotend', 'Objetivo boquilla C'), ('bed', 'Objetivo cama C')))
            elif action == 'tuning':
                payload = prompt_values((('speed', 'Velocidad 10..200 %'), ('flow', 'Flujo 50..150 %'), ('fan', 'Ventilador 0..100 %')))
            elif action == 'progress':
                payload['percent'] = float(input('Porcentaje 0..100: '))
            elif action == 'auto':
                answer = input('Avance automatico (s/n): ').strip().lower()
                if answer not in ('s', 'si', 'n', 'no'):
                    raise ValueError('Responde s o n')
                payload['enabled'] = answer in ('s', 'si')
            elif action == 'error':
                payload['message'] = input('Mensaje [Error de prueba]: ').strip() or 'Error de prueba'
            elif action == 'fault':
                payload['mode'] = input('Fallo (reject/drop/malformed/delay/truncated/fragment): ').strip()
                if payload['mode'] == 'delay':
                    payload['seconds'] = float(input('Demora en segundos [2]: ').strip() or '2')
            elif action == 'gcode':
                payload['script'] = input('G-code: ')
            client.action(action, **payload)
            print('\nOK: ' + selected['label'])
        except ValueError as error:
            print('\nError: ' + str(error))


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument('--url', default=DEFAULT_URL, help='Servidor falso (por defecto %(default)s)')
    result.add_argument('--json', action='store_true', help='Salida JSON para scripts')
    commands = result.add_subparsers(dest='command')
    for name in ('menu', 'status', 'files', 'pause', 'resume', 'cancel', 'complete', 'standby',
                 'recover', 'disconnect', 'disconnect_ws', 'emergency', 'shutdown', 'clear_faults'):
        commands.add_parser(name)
    serve = commands.add_parser('serve', help='Iniciar servidor interactivo en primer plano')
    serve.add_argument('--bind', default='0.0.0.0')
    serve.add_argument('--port', default=17125, type=int)
    start = commands.add_parser('start')
    start.add_argument('filename', nargs='?', default='01_prueba_1_min.gcode')
    start.add_argument('--seconds', type=float)
    for command, fields in (('temperatures', ('hotend', 'bed')), ('tuning', ('speed', 'flow', 'fan'))):
        sub = commands.add_parser(command)
        for field in fields:
            sub.add_argument('--' + field, type=float)
    commands.add_parser('progress').add_argument('percent', type=float)
    commands.add_parser('auto').add_argument('mode', choices=('on', 'off'))
    commands.add_parser('gcode').add_argument('script')
    commands.add_parser('error').add_argument('message', nargs='?', default='Error de prueba')
    fault = commands.add_parser('fault')
    fault.add_argument('mode', choices=('reject', 'drop', 'malformed', 'delay', 'truncated', 'fragment'))
    fault.add_argument('--seconds', type=float, default=2)
    commands.add_parser('history').add_argument('--limit', type=int, default=20)
    commands.add_parser('watch').add_argument('--interval', type=float, default=1)
    return result


def main():
    args = parser().parse_args()
    command = args.command or 'menu'
    if command == 'serve':
        server = Path(__file__).resolve().parents[1] / 'tests' / 'klipper_simulator.py'
        return subprocess.call([sys.executable, str(server), '--interactive', '--bind', args.bind, '--port', str(args.port)])
    try:
        client = Client(args.url)
        if command == 'menu':
            menu(client)
            return 0
        if command == 'watch':
            if not .1 <= args.interval <= 60:
                raise ValueError('Intervalo: 0.1..60 segundos')
            watch(client, args.interval)
            return 0
        if command == 'files':
            data = client.api('/__test/files')
            if not args.json:
                print_files(data['files'])
                return 0
        elif command == 'history':
            if args.limit < 1:
                raise ValueError('El limite debe ser positivo')
            if not args.json:
                print_history(client, args.limit)
                return 0
            data = client.api('/__test/requests')[-args.limit:]
        elif command == 'status':
            data = client.api('/__test/status')
        else:
            payload = {key: value for key, value in vars(args).items()
                       if key not in ('url', 'json', 'command') and value is not None}
            if command == 'auto':
                payload = dict(enabled=args.mode == 'on')
            data = client.action(command, **payload)
        print(json.dumps(data, ensure_ascii=False, indent=2) if args.json else status_text(data))
        return 0
    except (ValueError, URLError, TimeoutError, ConnectionError, OSError) as error:
        print('Error: ' + str(error), file=sys.stderr)
        print('Inicia el servidor con: moonraker_mock.cmd serve (CMD) o bash tools/moonraker_mock.sh serve (Bash)', file=sys.stderr)
        return 1
    except (EOFError, KeyboardInterrupt):
        print('\nMenu cerrado. El servidor sigue activo.')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
