# Inspección de pantallas — 0.5.0

2026-10-26, America/Bogota. Versión `0.5.0-screen-inspection`, ESP-IDF5.5.6,
cargada y verificada en COM8, ESP32-P4 v1.3, USB80:F1:B2:D1:7F:0B.
Aplicación2.824.416 bytes; partición factory4 MiB con33% libre.
Esptool verificó los hashes escritos de aplicación, bootloader y particiones.

Uso: `P` + Enter, `4` + Enter y una opción1–11. `12` recupera el modo normal;
`13` consulta el estado; `0` termina la inspección y vuelve a Pantallas. `M` y
cambiar a otra sección también la terminan. `screen show NOMBRE`, `screen status`
y `screen live` están disponibles como comandos completos.

La consola publica solicitudes y la tarea GUI aplica los cambios de LVGL.
La vista elegida permanece fija con los datos reales disponibles; no modifica
instantáneas de Klipper. Una capa transparente intercepta el táctil y los callbacks
de controles también rechazan eventos durante la inspección o una solicitud pendiente.
La pantalla permanece despierta con su brillo guardado. El modo no se guarda en NVS.
Los eventos de voz siguen recibiendo únicamente el estado real del backend.

Un diálogo de confirmación previo se cancela al entrar. Los avisos rechazados o
inciertos pendientes impiden iniciar la inspección; recibir uno durante ella
recupera el modo normal y muestra el aviso. Al salir se presenta el estado real
más reciente, sin reabrir confirmaciones anteriores ni reutilizar el toque previo.

Validación:

- 1860 comprobaciones nativas de navegación aprobadas, incluido el submenú,
  sus11 destinos y las rutas de salida.
- 64/64 escenarios con LVGL9.3 real y servicios simulados;112 eventos de widgets.
  Los11 casos nuevos cubren permanencia/restauración, solicitudes pendientes y
  sucesivas, bloqueo de acciones/ajustes/audio, reposo, avisos y retorno de controles.
- Once vistas800×480 renderizadas, sin etiquetas fuera de pantalla. Se revisaron las
  capturas de Ajustes y Detalles con el aviso de inspección; son imágenes de escritorio.
- 38/38 controles seriales en placa:11 destinos aplicados por GUI, permanencia,
  selección inválida, consulta numerada, salida con0/M, comandos completos,
  ajustes conservados y voces sin emisiones adicionales. Klipper permaneció en
  standby, conectado a192.168.0.235:7125; los objetivos de temperatura siguieron a0.

Audio83%, brillo59% y reposo1 minuto antes/después de la inspección. Sin acciones
de impresora ni comandos para escribir ajustes en la prueba. Sin fallos de ejecución
observados. Al terminar se recuperó el modo normal y COM8 quedó cerrado y disponible.
La verificación serial confirma procesamiento de solicitudes; la revisión visual
del LCD físico y la prueba manual del táctil requieren observación del usuario.

Evidencias: `01-build.log`, `02-host-regression.log`, `03-flash.log`, `04-device.*`,
`host-action-report.json`, capturas `inspection-*.png` y `firmware-manifest.json`.
Herramienta reproducible:

```powershell
python tools/verify_screen_inspection.py --port COM8 --reset --output verification/screen-inspection/04-device.log
```
