"""Deterministic printer model for the opt-in interactive Moonraker fixture."""
import copy
import math
import re
import shlex


FILES = [
    dict(path='01_prueba_1_min.gcode', seconds=60, colors=['#00C9F4'], changes=[],
         weight=3.2, hotend=200, bed=55, description='Prueba de un minuto'),
    dict(path='02_tres_colores_1m30s.gcode', seconds=90,
         colors=['#00C9F4', '#FF9D00', '#A855F7'], changes=[30, 60],
         weight=4.8, hotend=210, bed=60, description='Tres colores; pausa a los 30 y 60 segundos'),
    dict(path='03_pieza_rapida_30s.gcode', seconds=30, colors=['#22C55E'], changes=[],
         weight=1.4, hotend=195, bed=50, description='Pieza rapida para repetir ciclos'),
    dict(path='04_pieza_larga_3_min.gcode', seconds=180, colors=['#FF9D00'], changes=[],
         weight=10.5, hotend=220, bed=65, description='Prueba de pausa, velocidad, flujo y ventilador'),
]
FAULT_KEYS = ('delay_post', 'drop_post', 'reject_post', 'malformed_post', 'truncated_png', 'fragment_ws')


def number(value, low, high, label):
    try:
        result = float(value)
    except (TypeError, ValueError):
        raise ValueError(f'{label}: introduce un numero') from None
    if not math.isfinite(result) or not low <= result <= high:
        raise ValueError(f'{label}: rango {low}..{high}')
    return result


def profile(filename):
    return next((entry for entry in FILES if entry['path'] == filename), None)


def metadata(entry):
    count = len(entry['colors'])
    return dict(filename=entry['path'], estimated_time=entry['seconds'],
                filament_total=round(entry['weight'] * 335, 1), filament_weight_total=entry['weight'],
                filament_colors=entry['colors'], filament_weights=[entry['weight'] / count] * count,
                referenced_tools=list(range(count)), layer_count=20,
                thumbnails=[dict(width=size, height=size,
                                 relative_path=f".thumbs/{entry['path'][:-6]}-{size}.png")
                            for size in (48, 100, 300)])


def gcode_file(entry):
    return ('; ARCHIVO VIRTUAL DE PRUEBA - no ejecutar en una impresora real\n'
            f"; {entry['description']}\n; estimated_time = {entry['seconds']}\n"
            f"; filament_colors = {';'.join(entry['colors'])}\n"
            '; El simulador controla tiempos y cambios de color mediante su modelo.\n'
            'G90\nG28\n' + f"M104 S{entry['hotend']}\nM140 S{entry['bed']}\n"
            'G1 X100 Y80 Z5 F3000\nM104 S0\nM140 S0\n')


class Simulation:
    def __init__(self, options):
        self.options = options
        options.update(print_duration=0, duration_seconds=60, auto_progress=True,
                       extruder_temperature=25, extruder_target=0, bed_temperature=25, bed_target=0,
                       speed_factor=1, extrude_factor=1, fan_speed=0, homed_axes='',
                       position=[0, 0, 0, 0], state_message='', excluded_objects=[],
                       color_index=0, color_changes_done=0, color_change_pending=False,
                       pause_reason='', last_macro='')
        self.absolute = True
        self.extruder_absolute = True
        self.saved_modes = {}

    def details(self):
        o = self.options
        entry = profile(o['filename'])
        return dict(duration_seconds=o['duration_seconds'], auto_progress=o['auto_progress'],
                    pause_reason=o['pause_reason'], color_index=o['color_index'],
                    color_changes_done=o['color_changes_done'], color_change_pending=o['color_change_pending'],
                    colors=entry['colors'] if entry else [], last_macro=o['last_macro'])

    def actions(self):
        o = self.options
        ready = o['state'] == 'ready'
        active = o['job_state'] in ('printing', 'paused')
        definitions = [
            ('start', 'Iniciar archivo de impresion', ready and not active),
            ('pause', 'Pausar', ready and o['job_state'] == 'printing'),
            ('resume', 'Reanudar / confirmar cambio de color', ready and o['job_state'] == 'paused'),
            ('cancel', 'Cancelar impresion', active),
            ('complete', 'Completar ahora', active),
            ('standby', 'Limpiar trabajo y volver a espera', True),
            ('temperatures', 'Cambiar temperaturas', ready),
            ('tuning', 'Velocidad, flujo y ventilador', ready),
            ('progress', 'Fijar porcentaje manualmente', active),
            ('auto', 'Activar/desactivar avance automatico', True),
            ('error', 'Simular error de Klipper', True),
            ('recover', 'Recuperar Klipper / reiniciar', True),
            ('disconnect_ws', 'Forzar reconexion WebSocket', True),
            ('disconnect', 'Simular Klipper desconectado', True),
            ('history', 'Ver ordenes recibidas de la pantalla', True),
            ('fault', 'Inyectar fallo de comunicacion', True),
            ('clear_faults', 'Quitar fallos de comunicacion', True),
            ('emergency', 'Parada de emergencia simulada', True),
            ('shutdown', 'Apagado de host simulado', True),
            ('gcode', 'Enviar G-code al simulador', ready),
        ]
        return [dict(id=key, label=label, enabled=enabled) for key, label, enabled in definitions]

    def advance(self, seconds):
        o = self.options
        before = copy.deepcopy(o)
        for prefix, rate in (('extruder', 8), ('bed', 3)):
            current = o[f'{prefix}_temperature']
            target = max(25, o[f'{prefix}_target'])
            o[f'{prefix}_temperature'] = round(current + max(-rate * seconds, min(rate * seconds, target - current)), 2)
        if o['state'] == 'ready' and o['job_state'] == 'printing' and o['auto_progress']:
            duration = o['duration_seconds']
            next_progress = min(1, o['progress'] + seconds * o['speed_factor'] / duration)
            entry = profile(o['filename'])
            milestones = entry['changes'] if entry else []
            index = o['color_changes_done']
            if index < len(milestones) and o['progress'] < milestones[index] / entry['seconds'] <= next_progress:
                next_progress = milestones[index] / entry['seconds']
                o.update(job_state='paused', color_change_pending=True,
                         pause_reason=f'Cambio de color {index + 1}/{len(milestones)}: confirmar con Reanudar')
            consumed = max(0, (next_progress - o['progress']) * duration / o['speed_factor'])
            o['print_duration'] += consumed
            o['progress'] = next_progress
            o['position'] = [round(30 + next_progress * 160, 2), round(40 + next_progress * 80, 2),
                             round(.2 + next_progress * 9.8, 2), round(next_progress * 100, 2)]
            if next_progress >= 1:
                o.update(job_state='complete', pause_reason='')
        return o != before

    def action(self, action, payload):
        o = self.options
        available = {entry['id']: entry['enabled'] for entry in self.actions()}
        if action not in available or not available[action]:
            raise ValueError(f'Accion no disponible en este estado: {action}')
        if action == 'start':
            filename = payload.get('filename', FILES[0]['path'])
            entry = profile(filename)
            if not entry:
                raise ValueError('Archivo virtual desconocido')
            duration = number(payload.get('seconds', entry['seconds']), 1, 86400, 'Duracion')
            o.update(filename=filename, duration_seconds=duration, print_duration=0, progress=0,
                     job_state='printing', color_index=0, color_changes_done=0, color_change_pending=False,
                     pause_reason='', auto_progress=True, excluded_objects=[],
                     extruder_target=entry['hotend'], bed_target=entry['bed'], speed_factor=1,
                     extrude_factor=1, fan_speed=0)
        elif action == 'pause':
            o.update(job_state='paused', pause_reason='Pausa manual')
        elif action == 'resume':
            if o['color_change_pending']:
                o['color_changes_done'] += 1
                o['color_index'] += 1
            o.update(job_state='printing', color_change_pending=False, pause_reason='')
        elif action in ('cancel', 'complete'):
            o.update(job_state='cancelled' if action == 'cancel' else 'complete',
                     color_change_pending=False, pause_reason='')
            if action == 'complete':
                o['progress'] = 1
        elif action in ('standby', 'recover'):
            o.update(state='ready', state_message='', job_state='standby', filename='', progress=0,
                     print_duration=0, color_index=0, color_changes_done=0, color_change_pending=False,
                     pause_reason='', extruder_target=0, bed_target=0, fan_speed=0)
        elif action == 'temperatures':
            values = {}
            for key, upper in (('hotend', 300), ('bed', 120)):
                if key in payload:
                    values['extruder_target' if key == 'hotend' else 'bed_target'] = number(payload[key], 0, upper, key)
            if not values:
                raise ValueError('Indica hotend o bed')
            o.update(values)
        elif action == 'tuning':
            values = {}
            for key, low, high, target in (('speed', 10, 200, 'speed_factor'),
                                           ('flow', 50, 150, 'extrude_factor'), ('fan', 0, 100, 'fan_speed')):
                if key in payload:
                    values[target] = number(payload[key], low, high, key) / 100
            if not values:
                raise ValueError('Indica speed, flow o fan')
            o.update(values)
        elif action == 'progress':
            progress = number(payload.get('percent'), 0, 100, 'Porcentaje') / 100
            o.update(progress=progress, print_duration=progress * o['duration_seconds'],
                     color_change_pending=False, pause_reason='')
            entry = profile(o['filename'])
            count = sum(progress >= value / entry['seconds'] for value in entry['changes']) if entry else 0
            o.update(color_changes_done=count, color_index=count)
            if progress == 1:
                o['job_state'] = 'complete'
        elif action == 'auto':
            if not isinstance(payload.get('enabled'), bool):
                raise ValueError('enabled debe ser true o false')
            o['auto_progress'] = payload['enabled']
        elif action in ('error', 'emergency', 'shutdown', 'disconnect'):
            o.update(state='disconnected' if action in ('shutdown', 'disconnect') else 'shutdown',
                     state_message=payload.get('message', 'Emergencia simulada' if action == 'emergency' else 'Fallo simulado'))
            if action in ('error', 'emergency'):
                o.update(job_state='error', extruder_target=0, bed_target=0)
        elif action == 'fault':
            mode = payload.get('mode')
            mapping = dict(reject='reject_post', drop='drop_post', malformed='malformed_post',
                           delay='delay_post', truncated='truncated_png', fragment='fragment_ws')
            if mode not in mapping:
                raise ValueError('Fallo desconocido')
            value = number(payload.get('seconds', 2), 0, 30, 'Demora') if mode == 'delay' else True
            o[mapping[mode]] = value
        elif action == 'clear_faults':
            o.update({key: 0 if key == 'delay_post' else False for key in FAULT_KEYS})
        elif action == 'gcode':
            self.gcode(payload.get('script', ''))
        # disconnect_ws is handled by the transport, history by the console.

    def gcode(self, script):
        if not isinstance(script, str) or len(script) > 4096:
            raise ValueError('G-code invalido (maximo 4096 caracteres)')
        o = self.options
        for line in script.splitlines():
            line = line.split(';', 1)[0].strip()
            if not line:
                continue
            tokens = shlex.split(line)
            command = tokens[0].upper()
            params = dict(token.split('=', 1) for token in tokens[1:] if '=' in token)
            words = {key.upper(): float(value) for key, value in
                     re.findall(r'([XYZESF])\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)', ' '.join(tokens[1:]), re.I)}
            if command in ('PAUSE', 'RESUME', 'CANCEL_PRINT', 'FIRMWARE_RESTART', 'RESTART', 'SDCARD_RESET_FILE', 'M112'):
                self.action(dict(PAUSE='pause', RESUME='resume', CANCEL_PRINT='cancel', FIRMWARE_RESTART='recover',
                                 RESTART='recover', SDCARD_RESET_FILE='standby', M112='emergency')[command], {})
            elif command == 'SDCARD_PRINT_FILE':
                self.action('start', dict(filename=params.get('FILENAME', '')))
            elif command in ('M104', 'M109', 'M140', 'M190'):
                self.action('temperatures', {'hotend' if command in ('M104', 'M109') else 'bed': words.get('S', 0)})
            elif command in ('M220', 'M221', 'M106', 'M107'):
                key = 'speed' if command == 'M220' else 'flow' if command == 'M221' else 'fan'
                value = 0 if command == 'M107' else words.get('S', 255 if command == 'M106' else 100)
                self.action('tuning', {key: value / 255 * 100 if command == 'M106' else value})
            elif command == 'G28':
                axes = ''.join(axis for axis in 'xyz' if axis.upper() in line[3:].upper()) or 'xyz'
                for axis in axes:
                    o['position']['xyz'.index(axis)] = 0
                o['homed_axes'] = ''.join(axis for axis in 'xyz' if axis in o['homed_axes'] + axes)
            elif command == 'M84':
                o['homed_axes'] = ''
            elif command in ('G90', 'G91', 'M82', 'M83'):
                if command in ('G90', 'G91'):
                    self.absolute = command == 'G90'
                else:
                    self.extruder_absolute = command == 'M82'
            elif command in ('G0', 'G1'):
                position = o['position'][:]
                for index, axis in enumerate('XYZE'):
                    if axis in words:
                        value = words[axis] + (0 if (self.extruder_absolute if axis == 'E' else self.absolute) else position[index])
                        position[index] = number(value, 0 if axis != 'E' else -1e6,
                                                 (300, 250, 250, 1e6)[index], axis)
                o['position'] = position
            elif command == 'SAVE_GCODE_STATE':
                self.saved_modes[params.get('NAME', 'default')] = (self.absolute, self.extruder_absolute)
            elif command == 'RESTORE_GCODE_STATE':
                self.absolute, self.extruder_absolute = self.saved_modes.get(params.get('NAME', 'default'), (True, True))
            elif command == 'EXCLUDE_OBJECT':
                name = params.get('NAME')
                if params.get('RESET') == '1':
                    o['excluded_objects'] = []
                elif name and name not in o['excluded_objects']:
                    o['excluded_objects'].append(name)
            elif command == 'M600':
                self.action('pause', {})
                o['pause_reason'] = 'Cambio de color manual'
            else:
                o['last_macro'] = line
