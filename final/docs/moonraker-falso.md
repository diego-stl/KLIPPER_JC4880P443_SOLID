# Moonraker interactivo de pruebas

Simula una impresora y recibe ordenes de la pantalla por HTTP, con estados y
progreso por WebSocket. Ambos usan el puerto **17125**. El servidor no se conecta
a impresoras ni ejecuta ordenes sobre hardware. Solo necesita Python 3, sin
dependencias adicionales.

En este PC la pantalla se conecta a **192.168.0.161:17125**. En Ajustes > WI-FI Y
MOONRAKER > Moonraker se escribe la IP sin `http://` ni rutas. Si cambia la IP del
PC, actualizarla en la pantalla. El PC y la pantalla deben compartir la red local.

## Abrir el menu

Ejecutar desde la raiz del proyecto. El servidor debe estar activo; cerrar el menu
no lo detiene.

CMD o PowerShell:

```bat
tools\moonraker_mock.cmd
```

Bash (incluido Git Bash):

```bash
bash tools/moonraker_mock.sh
```

Desde otro equipo con una copia del cliente y Python 3:

```bash
python3 tools/moonraker_console.py --url http://192.168.0.161:17125
```

El menu muestra estado de Klipper y del trabajo, archivo, porcentaje, tiempo,
temperaturas, velocidad, flujo, ventilador, color, conexiones y fallos activos.
Las opciones que no aplican al estado actual aparecen como **no disponible**.
Enter actualiza el estado, `f` lista archivos, `v` abre el monitor en vivo y `0`
cierra el menu. Ctrl+C sale del monitor y vuelve al menu.

Las opciones numeradas permiten iniciar, pausar, reanudar, cancelar, completar,
volver a espera, modificar temperaturas y ajustes, fijar progreso, controlar el
avance automatico, simular errores, recuperar Klipper, forzar reconexion, consultar
ordenes, inyectar fallos y enviar G-code.

## Archivos disponibles en la pantalla

| Archivo | Tiempo activo a velocidad 100 % | Prueba |
| --- | --- | --- |
| `01_prueba_1_min.gcode` | 60 s | Impresion de un minuto, como la primera prueba |
| `02_tres_colores_1m30s.gcode` | 90 s | Turquesa, naranja y violeta; pausas a los 30 y 60 s |
| `03_pieza_rapida_30s.gcode` | 30 s | Repetir rapidamente inicio, final y limpieza |
| `04_pieza_larga_3_min.gcode` | 180 s | Probar pausa, cancelacion, velocidad, flujo y ventilador |

Son archivos virtuales con metadatos y miniaturas de sus colores. Aparecen en la
lista de archivos de la pantalla y se pueden seleccionar e imprimir desde ella.
Sus rutas tambien admiten descarga HTTP para inspeccion; representan escenarios
del simulador y no estan preparados para imprimir piezas reales.

En el archivo de tres colores, la pantalla pasa a **Pausa** en cada cambio.
Pulsar **Reanudar** en la pantalla o en el menu confirma el siguiente color.
El servidor conserva el porcentaje y el contador durante la pausa: son 90 segundos
de impresion mas el tiempo que tardes en confirmar los dos cambios.

La velocidad modifica el avance. Al 200 %, una impresion de 60 segundos termina
en 30 segundos activos. Si se indica una duracion personalizada al iniciar, se
escala tambien el momento de los cambios de color dentro del archivo.

## Comandos directos para scripts

CMD y PowerShell:

```bat
tools\moonraker_mock.cmd status
tools\moonraker_mock.cmd files
tools\moonraker_mock.cmd start 01_prueba_1_min.gcode
tools\moonraker_mock.cmd pause
tools\moonraker_mock.cmd resume
tools\moonraker_mock.cmd cancel
tools\moonraker_mock.cmd standby
tools\moonraker_mock.cmd start 02_tres_colores_1m30s.gcode
tools\moonraker_mock.cmd temperatures --hotend 200 --bed 60
tools\moonraker_mock.cmd tuning --speed 120 --flow 95 --fan 50
tools\moonraker_mock.cmd history
tools\moonraker_mock.cmd watch
```

En Bash usar los mismos argumentos con `bash tools/moonraker_mock.sh`:

```bash
bash tools/moonraker_mock.sh status
bash tools/moonraker_mock.sh start 01_prueba_1_min.gcode
bash tools/moonraker_mock.sh pause
bash tools/moonraker_mock.sh resume
bash tools/moonraker_mock.sh --json status
```

Otros comandos: `complete`, `auto on`, `auto off`, `progress 42`, `error "Mensaje"`,
`recover`, `disconnect`, `disconnect_ws`, `emergency`, `shutdown`,
`gcode "G28 X Y"`, `fault reject`, `fault drop`, `fault malformed`,
`fault delay --seconds 2`, `fault truncated`, `fault fragment` y `clear_faults`.
`--help` muestra la ayuda. `--url` y `--json` se escriben antes del comando.

`shutdown` y `disconnect` simulan la desconexion del host/Klipper; el proceso del
servidor permanece activo para permitir `recover`. Los fallos de comunicacion
afectan a las peticiones de la pantalla, mientras el menu conserva acceso para
quitarlos con `clear_faults`.

## Iniciar o actualizar el servidor

PowerShell, en segundo plano:

```powershell
.\tools\moonraker_mock.ps1 -Background
```

Si hay una version anterior en marcha:

```powershell
.\tools\moonraker_mock.ps1 -Background -Restart
```

El reinicio verifica identidad y proceso, detiene exclusivamente este simulador y
arranca la version actual. Inicia en espera y reinicia el historial en memoria.
El script muestra el PID para detenerlo y los registros en `.cache`.

CMD, servidor en primer plano (dejar esta terminal abierta):

```bat
tools\moonraker_mock.cmd serve
```

Bash, servidor en primer plano:

```bash
bash tools/moonraker_mock.sh serve
```

Abrir el menu en una segunda terminal. Ctrl+C en la terminal del servidor lo
detiene. Si Python no se encuentra, definir `MOONRAKER_PYTHON` con su ejecutable
para los lanzadores CMD/Bash, o usar `-Python` en el lanzador PowerShell.
Si Windows muestra un aviso de firewall para Python, permitir acceso desde la red
privada.

## Protocolo y verificacion

- `GET /__test/identity`: identidad exclusiva del simulador.
- `GET /__test/status`: estado, clientes, simulacion y fallos.
- `GET /__test/actions`: acciones y disponibilidad actual.
- `GET /__test/files`: catalogo de escenarios.
- `GET /__test/requests`: historial de ordenes de la pantalla y G-code de consola.
- `POST /__test/action`: accion validada, por ejemplo `{"action":"pause"}`.
- `POST /__test/control`: controles de bajo nivel del fixture original.

La consola verifica identidad y modo interactivo antes de permitir acciones y no
sigue redirecciones. El servidor interactivo admite las ordenes de la pantalla:
inicio de archivo, PAUSE, RESUME, CANCEL_PRINT, SDCARD_RESET_FILE,
FIRMWARE_RESTART, emergencia, apagado simulado, homing, movimientos relativos,
extrusion, objetivos de temperatura, velocidad, flujo, ventilador y exclusion.
Las temperaturas convergen gradualmente al objetivo; M109/M190 no bloquean hasta
alcanzarlo. Otros G-codes o macros quedan registrados sin emular su contenido.

El fixture original sigue disponible, sin avance ni efectos de G-code, al ejecutar
`python tests/klipper_simulator.py` sin `--interactive`. Las pruebas existentes que
llaman a `make_server()` conservan ese modo.

```powershell
& 'C:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe' tests/klipper_simulator.py --self-test
& 'C:\Espressif\tools\python\v5.5.6\venv\Scripts\python.exe' tests/moonraker_interactive_regression.py
```
