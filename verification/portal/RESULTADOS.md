# PORTAL / Klipper — resultados

Fecha: 2026-10-24, America/Bogota. Firmware0.2.0-portal, IDF5.5.6, LVGL9.3.0.
P4 v1.3/360 MHz, PSRAM32 MiB a200 MHz, Flash16 MiB; COM8.

## Resultado

Aplicación final2.101.440 bytes, factory4 MiB. Las escrituras de bootloader,
tabla y aplicación fueron verificadas por esptool. Se conservan NVS/phy y el C6.
R90/USB posterior izquierdo aceptados; el usuario confirmó textos y menús PORTAL.

## Verificación

- 41 static_assert de las reglas usadas por el backend: estado, transferencias,
 clasificación HTTP, descarte de lote y valores numéricos.
- 36 escenarios y 78 eventos LVGL reales en host; 11 vistas 800 × 480 sin etiquetas
 fuera del marco. Colores y persistencia del aviso incierto comprobados.
- En P4/simulador: arranque/PSRAM/GT911, configuración por consola, snapshot WS,
 listas15/20/16, metadatos/PNG100 × 100 y diez acciones HTTP.
- 19 comprobaciones de transporte/casos límite correctas: recepción de las acciones,
 primera orden incierta recibida una vez, segunda descartada, Unknown+Rejected,
 recuperación de snapshot fragmentado, progreso, delta cero, pausa, PNG truncado
 no publicado, miniatura recuperada y reconexión STA+WebSocket.
- Servidor real **192.168.0.235:7125** guardado por consola. Estado listo y temperaturas
 reales alrededor de19,4°C/18,8°C,2 G-codes,20 macros,0 objetos durante reposo y
 metadatos con miniatura PNG100 × 100 de1758 bytes recibidos desde la placa.
- Arranque final tras reset: Wi-Fi recargada, endpoint persistido, WS+snapshot y
 Klipper listo, sin reconfigurar ni recompilar.

Los POST de pruebas automáticas fueron solo al simulador del PC192.168.0.161:17125,
verificado por identidad positiva antes de habilitar el driver. Las comprobaciones
del servidor real fueron consultas; no iniciaron impresión ni calentamiento.

## Evidencias principales

- `10-mock-device.log/json`: prueba normal en el P4.
- `11-mock-normal-requests.json`: órdenes recibidas por el simulador.
- `11-mock-resilience.log/json`:19 comprobaciones de transporte.
- `13-real-ready.log/json`, `14-real-http-lists.log/json`: lecturas reales.
- `15-build-final.log`, `16-flash-final.log`, `17-final-boot-persistence.log/json`.
- `aceptacion-usuario.md`: observación física.
- `firmware-0.2.0/`: binarios/configuración/lock/SHA-256.
- `tests/portal_host/output/`: render y acciones asociados a hashes de fuentes.

El PC observó inicialmente `mcu 'mcu': Unable to connect`; por eso una primera
comprobación esperaba error. El servidor pasó a listo durante la integración;
`12-real-device.json` registra esa expectativa antigua fallida y las comprobaciones
13/14/17 confirman el estado listo posterior. No es un fallo de la conexión del panel.

## Límites

La aceptación visual/navegación no equivale a imprimir un trabajo real ni a probar
movimientos, calentamientos o apagado sobre la impresora. Esas rutas HTTP se
validaron con el simulador y eventos de la UI. No se realizó una prueba prolongada
de estrés, corte de alimentación o todos los fallos posibles de AP/SDIO.
Audio, LED, NERV, autenticación/API key/TLS nuevos y OTA quedan fuera de esta etapa.
