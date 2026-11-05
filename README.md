# KLIPPER_JC4880P443_SOLID

Panel Klipper/Moonraker para Guition JC4880P443C, ESP32-P4 v1.3.
**PORTAL nativo 800 × 480, R90 aceptado**, con USB posterior izquierdo.

## Repositorio y ramas

`main` conserva la base estable y `develop` integra el desarrollo. Cada funcion
o correccion se trabaja en una rama propia y se integra mediante pull request.
Ver el [flujo de trabajo con Git y GitHub](docs/flujo-git.md).

El repositorio incluye codigo, recursos, documentacion e informes/capturas
seleccionados. Los binarios, respaldos y logs de verificacion permanecen locales.

## Estado — 2026-10-27, America/Bogota

Firmware **0.6.5-backlight-100hz** compilado y cargado en COM8. La retroiluminación
usa PWM de **100 Hz** en GPIO23 para comparar el zumbido a distintos niveles de brillo.
La prueba anterior a 1 kHz no lo eliminó; el usuario confirmó que **100 Hz
solucionó el zumbido**.
Conserva las funciones de impresión de 0.6.3. La base PORTAL0.2.0
fue aceptada por el usuario: textos correctos y menús Ajustes, G-codes y Macros operativos.
La primera etapa de navegación serial `M/P/A/W/K/D/L` está cargada y verificada en placa;
ver [resultados de la carga y navegación](verification/serial-menu/RESULTADOS.md).
La actualización incorpora seis botones uniformes, detalles del último trabajo,
miniaturas grandes, filtrado de filamentos/macros y configuración táctil de red.
Pasaron 156 escenarios con 533 eventos LVGL y once capturas sin etiquetas fuera de pantalla.
La revisión0.6.1 usa nombres sin miniaturas a la izquierda y preview300 + colores
sin rótulos/cantidades a la derecha; se eliminó la precarga por archivo. La IP usa
teclado numérico con punto y GLaDOS vuelve a los estados principales.
Ver [revisión de flujo](docs/revision-flujo-0.6.1.md), [catálogo](docs/implementacion-portal-0.6.md) y
[resultados de flujo](verification/ui-flow-update/RESULTADOS.md).
La [propuesta gráfica v2](design/revision-0.6.1/README.md) fue aprobada e integrada:
32 iconos con fuentes de 64 px y controles secundarios de 32 px, boquilla/cama
elegidas y logos PNG transparentes. Se conservaron textos, GLaDOS y el flujo de G-codes.
Ver [recursos](docs/assets-portal.md) y [resultados gráficos](verification/ui-artwork-update/RESULTADOS.md).
La versión 0.6.3 reemplaza Toolhead por **Ajustes de impresión** en impresión y pausa:
velocidad, flujo y ventilador de capa con valor actual/propuesto y Aplicar por fila.
Imprimir abre inmediatamente el estado; espera la confirmación real de Klipper.
Ver [contrato de ajustes](docs/ajustes-impresion-0.6.3.md) y
[resultados actuales](verification/print-settings-update/RESULTADOS.md).
La inspección de las once vistas desde `P` → `4` sigue disponible.
Wi-Fi y Moonraker se configuran desde Ajustes o por consola y se guardan en NVS.
Destino real confirmado: **192.168.0.235:7125**, guardado mediante consola;
no está incorporado como valor predeterminado en las fuentes.
Las siete voces GLaDOS y los avisos por estados de Klipper están integrados;
Sonido silencia también el arranque y vacía los avisos pendientes. La lectura actual
previa a la carga 0.6.3 confirma volumen 82%, brillo 59% y reposo 1 minuto; se conservan esos ajustes.
Audio usa ES8311 + NS4150, sin activar el micrófono.
El usuario confirmó que esta placa no incorpora LED controlable; esa función queda excluida.

Ver [resultados y evidencias](verification/portal/RESULTADOS.md).

## Funciones

- Arranque, recuperación de conexión, espera, impresión, pausa, completado, cancelación y error.
- Temperaturas/objetivos a la izquierda y porcentaje sin contadores de tiempo en impresión/pausa.
- Quince G-codes recientes con lista de nombres, vista previa300px e impresión directa con bloqueo de doble toque.
- Al imprimir se abre el estado con inicio pendiente; la vista sigue la confirmación real de Klipper.
- Ajustes de velocidad, flujo y ventilador en impresión/pausa, sin controles de movimiento del cabezal.
- Detalles del último trabajo con miniatura300px, resultado y limpieza protegida del estado.
- Hasta veinte macros públicas; `_...` ocultas y `variable_portal_visible` opcional en Klipper.
- Filamentos por herramienta; se omiten consumos cero conocidos y se conserva la diferencia entre cero y dato ausente.
- Ajustes accesibles desde arranque/error; Wi-Fi y Moonraker táctiles con teclado y trabajo de red asíncrono.
- Filtro compartido de pausa para UI/audio, con soporte opcional de indicadores de timelapse.
- Toolhead en espera: objetivos manuales, presets PLA/PETG/TPU/ABS, enfriar, pasos1/5/10/20/50/100 mm,
  movimientos relativos con SAVE/RESTORE_GCODE_STATE, homing y motores OFF.
- Pausa/reanudar/cancelar, extrusión/retracción10 mm, emergencia, FIRMWARE_RESTART y apagado del host.
- Hasta16 objetos: lista y mapa con límites XY recibidos de Klipper.
- Brillo5–100% por PWM de100 Hz, reposo1/5/10 minutos y primer toque para despertar.
- Órdenes pendientes/aceptadas/rechazadas/inciertas. No se reintentan POST inciertos;
  se descarta el resto del lote y el aviso permanece hasta cerrarlo.
- Las acciones se ligan a la generación del destino; una confirmación anterior no
  puede enviar una orden a otro servidor después de cambiar la configuración.

En Orca: `100x100/PNG,300x300/PNG`. Detalle prefiere PNG300 hasta128KiB;
la lista no descarga imágenes ni metadatos por archivo. PNG48 se ignora.
Sin miniatura válida se usa Aperture + **NO DATA**. No incorpora carga/borrado de G-codes ni OTA.

## Consola

Abrir esta carpeta como proyecto y ejecutar:

```powershell
.\tools\idf.ps1 monitor -Port COM8
```

USB Serial/JTAG,115200; salir con Ctrl+]. Cerrar el monitor antes de capturas automáticas.

La consola incorpora navegación e inspección de pantallas. El menú
principal se muestra al arrancar la consola. Escribir **letra + Enter** (mayúscula
o minúscula): `M` principal, `P` pantallas/táctil, `A` audio, `W` Wi-Fi,
`K` IP/conexión de Klipper, `D` diagnóstico del sistema y `L` logs.
Cada sección tiene opciones numeradas y su propio prompt. `0` o `M` vuelve al
principal (en inspección, `0` termina la inspección y vuelve a Pantallas);
Enter vacío muestra otra vez la sección actual. Se puede cambiar de
sección con cualquiera de sus letras y ejecutar comandos completos desde todas.
`help`/`H` muestra la ayuda y el menú actual. `Esc` cancela la entrada.

`W` ahora abre el menú Wi-Fi; configurar la red se hace con `W` → `3` o con
`config`. `W` → `7` pide escribir `SI` antes de borrar la red. El comando completo
`forget` conserva su comportamiento directo. Los formularios de red, contraseña
y host/IP conservan su lectura original: las letras son datos o las opciones
propias del formulario, sin activar atajos de navegación.

`P` → `4` abre la inspección: opciones1–11 para arranque, espera, impresión,
pausa, toolhead, archivos, macros, objetos, ajustes, error y detalles. La vista queda
fija con datos reales disponibles y un aviso de solo lectura; no se simula el estado
de Klipper ni se generan voces por cambiar de vista. Los controles táctiles quedan
bloqueados y la pantalla permanece despierta, con el brillo guardado. `12` recupera
el funcionamiento normal y `13` consulta el estado. `0`, `M` o cambiar de sección
también termina la inspección y vuelve al estado real más reciente. La selección
no se guarda en NVS. Un aviso de orden rechazada/incierta pendiente impide iniciar
la inspección; si llega uno durante ella, se recupera el modo normal para mostrarlo.

Comandos equivalentes: `screen list`, `screen show NOMBRE`, `screen status` y
`screen live`. Nombres: `startup`, `standby`, `printing`, `paused`, `toolhead`,
`files`, `macros`, `exclude`, `settings`, `error`, `details`. `SCREEN_REQUEST`
confirma recepción; `SCREEN_APPLIED` confirma que la tarea GUI procesó la solicitud.
No acredita una inspección visual del LCD físico.

La prueba dedicada del táctil, el diagnóstico de memoria/versión/reinicio
y los niveles de log por módulo aparecen como **pendientes**. Volumen y reproducción
de una voz muestran el comando completo que se debe escribir.

| Comando + Enter | Función |
| --- | --- |
| `M`, `P`, `A`, `W`, `K`, `D`, `L` | Menú principal y secciones de debug |
| `P` → `4` o `screen show NOMBRE` | Inspección de vistas con controles bloqueados |
| `screen live` / `screen status` | Terminar o consultar la inspección |
| `help` / `H` | Comandos completos y menú actual |
| `config` o `W` → `3` | Elegir red y guardar contraseña oculta |
| `moonraker` / `I` | Host/IP y puerto interactivos |
| `moonraker set HOST PUERTO` | Destino sin protocolo ni rutas |
| `status` | Red, destino, brillo y reposo |
| `connect`, `scan`, `check`, `reconnect-test` | Diagnósticos de red |
| `forget` | Borrar solo la configuración Wi-Fi del proyecto |
| `panel help`, `panel status` | Diagnóstico Klipper, temperaturas y metadatos |
| `panel files`, `panel macros`, `panel objects`, `panel metadata` | Consultas de lectura |
| `diag on` / `diag off` | Trazas periódicas GUI/táctil |
| `audio status` | Códec, volumen, reproducción, cola y contadores |
| `audio on`, `audio off` | Sonido global persistente; OFF detiene y vacía pendientes |
| `audio volume 0..100` | Volumen persistente; cero detiene y vacía pendientes |
| `audio test` | Tono explícito1 kHz/500 ms, respetando silencio y volumen |
| `audio stop` | Detener y vaciar pendientes sin cambiar el ajuste de sonido |
| `audio sounds`, `audio play NOMBRE` | Listar/reproducir voces: wakeup, printing, pause, cancel, error, complete, standby |
| `audio events` | Diagnóstico de transiciones, referencia de conexión y contadores por voz |

Se conserva `jc_wifi/station_v1`; los ajustes de aplicación usan `jc_panel/settings_v1`.
Audio usa `jc_audio/settings_v1`, con valor inicial activado/50%; el usuario eligió finalmente75%.
La voz de arranque respeta ese interruptor y el volumen guardados.
No se imprimen contraseñas ni se borra toda NVS ante errores. Se usan HTTP/WS de la
red local con la autorización configurada en Moonraker; no se añade formulario API key/TLS.

## Compilación

```powershell
.\tools\idf.ps1 build
.\tools\idf.ps1 size
.\tools\idf.ps1 flash -Port COM8
```

ESP-IDF5.5.6, LVGL9.3.0, GT911/touch1.2.1, Hosted2.12.13, Wi-Fi Remote1.6.5
y WebSocket Client1.6.1, fijados en manifiestos/lock. Audio usa esp_codec_dev1.3.4
con I²C master nuevo y el bus compartido existente a100 kHz. P4 pre-v3 a360 MHz, Flash16 MiB
y PSRAM32 MiB x16 a200 MHz. Hosted se inicia después de la GUI; se excluye su constructor temprano.
Panel físico480 × 800; UI800 × 480, R90 y tres buffers RGB565 de768.000 bytes en PSRAM.
Aplicación2.859.264 bytes; factory4 MiB con32% libre.

`partitions.csv` conserva NVS0x9000/0x6000 y phy_init0xF000/0x1000; solo amplía
factory de1500 KiB a4 MiB en0x10000. No borra toda Flash, no cambia eFuses ni actualiza el C6.

## Componentes y pruebas

`board_jc4880`/`st7701_sequence`: hardware; `display_orientation`/`gt911_input`/`gui_port`:
transformación y propietario LVGL; `portal_assets`/`portal_ui`: presentación;
`device_network`: Hosted, consola y NVS; `klipper_client`: WS/HTTP y cola;
`panel_console`: lecturas y pruebas protegidas. Ninguna tarea de red llama LVGL.
`audio_service`: un worker posee ES8311/I²S TX; GPIO11 habilita el amplificador
en alto. MCLK13/BCLK12/WS10/DOUT9, I²C SDA7/SCL8, dirección0x18; sin RX.

## Audio

En Ajustes: interruptor Sonido, volumen0–100% y botón PROBAR AUDIO. El interruptor
silencia todo y descarta el trabajo pendiente; activarlo no reproduce la cola anterior.
El estado de sonido y el volumen se conservan al reiniciar. PROBAR AUDIO reproduce
la voz de despertar; `audio test` conserva el tono. Se desconoce la impedancia/potencia
del altavoz conectado.

Se conservan las siete grabaciones originales de GLaDOS: arranque, imprimiendo/reanudar,
pausa, cancelación, error, completado y espera. `audio_events` utiliza instantáneas
reales del backend; la primera sincronización, reconexión y cambio de destino son
silenciosos. Desconexión y cambios de texto por sí solos no generan voz. Durante
silencio se sigue actualizando el estado, para no reproducir avisos atrasados al activarlo.
La voz de arranque se decide una sola vez, después de inicializar audio y cargar NVS;
si está silenciada se descarta. El micrófono queda sin activar.

Las grabaciones WAV originales ocupan640.218 bytes de Flash, con639.168 bytes PCM.
El parser reproduce exactamente el chunk `data`; excluye metadatos LIST/id3. La
conversión conserva mono22.050Hz→estéreo44.100Hz como en el origen. La versión0.4.1
aplica30% de amplitud a las voces antes de I²S para igualar el pico máximo del tono
validado (±9830). La curva de volumen permanece igual. Recursos, SHA256 y pruebas:
`components/audio_service/README.md` y `components/audio_service/assets/provenance.json`.

El amplificador permanece apagado hasta preparar códec y DMA. OFF/STOP bajan GPIO11
inmediatamente; invalidan la generación y vacían la cola. Los ajustes se guardan después
desde el worker (`audio status`: pending=0 confirma guardado). El worker comprueba
esa generación antes de habilitar el amplificador y durante cada bloque, y limpia
todo el anillo DMA antes de una prueba nueva. Si falla el audio, PORTAL continúa.

La herramienta histórica0.3 `tools/verify_audio_device.py` comprueba tono manual al50%, mute durante reproducción,
rechazo de pruebas en silencio, vaciado de cola, ausencia de reanudación y persistencia
tras reset. Solo admite la identidad USB del JC4880 ya verificado. Consulta Klipper
por lectura; no inicia impresiones, movimientos ni calentamiento. El resultado audible
requiere observación del usuario.

Prueba real `verification/audio/03-device-test.json`:13/13 controles aprobados;
ES8311 identificado en0x18 con registros83/11, tono completado, corte durante
reproducción, cola vaciada y silencio/50% conservados tras reset. PORTAL y Moonraker
continuaron activos. Observación del usuario: tono audible a volumen bajo y zumbido
muy leve con pantalla encendida; aceptación acústica pendiente. Ver
`verification/audio/user-observation.json`. Las ejecuciones01/02 conservan la evidencia
del ajuste de sincronización de la herramienta y de una reconexión WiFi durante la captura.
El usuario confirmó después que66% es un volumen aceptable y que el zumbido persiste
con Sonido desactivado. La consola confirmó66% guardado, brillo90% y reposo1 minuto;
se conserva la curva de volumen. El usuario localizó el zumbido en la propia placa/pantalla
y pidió ignorarlo; no forma parte del trabajo de audio ni se modifica la retroiluminación.

El 2026-10-27 se reabrió ese diagnóstico: el usuario comprobó que el volumen no
afecta el zumbido, que es máximo cerca del70% de brillo y desaparece al5% y100%.
La versión0.6.4 cambia únicamente la frecuencia de brillo de5 kHz a1 kHz y añade
una lectura de frecuencia al arranque. El esquema conecta el PWM a EN del MP3202,
cuya ficha recomienda1 kHz o menos para ese método. La carga conserva los ajustes
leídos antes de actualizar; la comprobación acústica se realiza en la placa.
Ver [carga y verificación](verification/backlight-1khz/RESULTADOS.md).

El usuario informó después que a1 kHz el zumbido persiste y solo desaparece al100%.
Aclara que pudo haber sido constante también antes de la prueba. A petición suya,
la versión0.6.5 cambia la frecuencia a100 Hz para comparar con espControl.
El usuario confirmó después que el zumbido quedó solucionado a100 Hz.
Ver [carga y verificación a100 Hz](verification/backlight-100hz/RESULTADOS.md).

`tools/verify_audio_voices.py` verifica las siete voces, el silencio de arranque,
interrupción/vaciado y estados por WebSocket de un simulador local identificado.
Preserva/restaura servidor, volumen, sonido y preferencias de pantalla; el simulador
no conecta a una impresora. La primera prueba0.4 con voz sin atenuar provocó brownout
al iniciar wakeup; la aplicación0.3 estable se recuperó y se restauró Moonraker.
Se conserva esa evidencia en `verification/audio-voices/03-device-voices.*`.
La versión corregida0.4.1 pasó34/34 comprobaciones en placa al66%, sin nuevos
brownouts ni fallos de pantalla/táctil. Ver `verification/audio-voices/10-device-voices-limited.json`:
siete voces manuales, interrupción/vaciado, arranque con/sin silencio, transiciones
de los siete avisos y reconexión silenciosa. Los estados de impresión se generaron
en el simulador local; recibió cero acciones de impresora. Al terminar se restauró
192.168.0.235:7125 y se guardó inicialmente66%, verificado en
`verification/audio-voices/11-final-settings.json`. Tras oír las voces claras, el usuario
eligió75%. `verification/audio-voices/12-final-volume75.json` confirma8/8 controles:
75% guardado y recargado tras reset, voz de arranque y cancelación larga completadas,
cola vacía y conexión real, sin nuevos brownouts ni fallos. Ese es el estado final.
Binarios, configuración y SHA256: `verification/audio-voices/firmware-0.4.1/`.

Fuentes: esquemas01/02 y mapa de pines conservados en
`../JC4880P443 BASE/docs/reference/`, [NS4150](https://dl.espressif.com/dl/schematics/NS4150B.pdf)
y [esp_codec_dev1.3.4](https://components.espressif.com/components/espressif/esp_codec_dev/versions/1.3.4).

## Verificación

- 59 static_assert del modelo, transferencias, lote, valores numéricos, metadatos y política de pausa/reset.
- 107 escenarios con174 eventos LVGL reales y11 capturas800 × 480 sin etiquetas fuera de pantalla.
  Los casos de audio de escritorio usan un mock silencioso; no validan el circuito físico.
- Prueba en P4 contra simulador: acciones HTTP, respuesta incierta, fragmentación, PNG truncado y reconexión.
- Servidor real0.6: estado/temperaturas,4 G-codes,20 macros públicas y PNG300 × 300 verificados por lecturas.
- 38 controles seriales de inspección en0.6; ajustes recargados y conservados. Revisión visual/táctil física pendiente de observación.

Los POST automáticos de prueba fueron exclusivamente al simulador identificado. Las
consultas reales no iniciaron impresión ni calentamiento.

Capturas: `tests/portal_host/output/`. Binarios/configuración/hashes actuales:
`verification/ui-flow-update/firmware-0.6.1/`; la evidencia histórica0.6 y0.2 está en `verification/ui-update/` y `verification/portal/firmware-0.2.0/`. Detalles: [backend](docs/klipper_backend.md),
[recursos](docs/assets-portal.md), [pruebas](docs/klipper_console_tests.md).

R90 aceptado: `verification/orientation/aceptacion-r90.md`. El binario de prueba0.1.0
se restaura con `tools/restore_orientation.ps1 -Port COM8`; la base original permanece
en `../JC4880P443 BASE`. El prototipo histórico está en `design/portal-800x480/index.html`;
la distribución0.6 y sus capturas LVGL se describen en `docs/implementacion-portal-0.6.md`.
