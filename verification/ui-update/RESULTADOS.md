# PORTAL 0.6.0 — resultados de implementación

2026-10-26, America/Bogota. Firmware `0.6.0-portal-controls` compilado y cargado
en COM8, ESP32-P4 v1.3, identidad USB `80:F1:B2:D1:7F:0B`.

Aplicación **2.826.608 bytes**, factory4MiB con33% libre. SHA256 instalado:
`8aa5e222146fa36ac22d7c3888de3165af19480be255d4258ba8e6299e56a111`.
Esptool verificó el hash escrito. Solo se escribió aplicación en0x10000;
no se escribió NVS, bootloader ni tabla de particiones.

## Cambios entregados

- Logos e iconos renovados; indicador Klipper14px; Ajustes en arranque/error.
- Rejilla2×3 con botones208×124 en estados principales; temperaturas a la
  izquierda y porcentaje sin contadores en impresión/pausa.
- Último trabajo con resultado, miniatura300 y reset del estado ligado a
  trabajo/servidor, conservando su resumen en RAM.
- Lista con imágenes96, selección240, impresión/pausa200 y detalles300.
  Se usa PNG100/300 de Orca; PNG48 se ignora. Aperture + NO DATA como respaldo.
- Impresión directa con revalidación de estado/destino y bloqueo de doble toque.
- Filamentos por herramienta, cero frente a ausencia y referencias explícitas.
- Macros `_...` ocultas antes del límite; configuración opcional
  `variable_portal_visible: False` en la impresora.
- Wi-Fi/Moonraker táctiles, teclado, escaneo, SSID manual, contraseña oculta,
  guardado y prueba de conexión mediante el worker de red.
- Política de pausa compartida UI/audio:2500ms y señal opcional de timelapse
  acotada a15s. Reinicio directo de Klipper en Error.
- Catálogo de las once vistas y explicación de opciones10/12/13.

Descripción y catálogo completos: `../../docs/implementacion-portal-0.6.md`.

## Verificación

| Capa | Resultado | Evidencia |
| --- | --- | --- |
| UI y LVGL9.3 reales, servicios simulados |101/101 escenarios,167 eventos,0 recortes |`host-action-report.json` |
| Capturas LVGL |11 vistas800×480 y casos ampliados |`contact_sheet.png`, `qa_*.png` |
| Políticas backend productivas |59 static_assert en5 archivos; compilación PASS |`backend-regression.log` |
| Validación red/credenciales |PASS; puertos/host/SSID/WPA/PSK |`network-regression.log` |
| Navegación serial nativa |1860 comprobaciones PASS |`network-regression.log` |
| Compilación ESP-IDF5.5.6 |PASS |`build-final.log` |
| Arranque e inspección en placa |38/38 controles PASS |`device-inspection.json` |
| Lecturas reales de metadatos y ajustes |7/7 controles PASS |`device-metadata.json` |

Los hashes de las fuentes de la UI y del conjunto de pruebas coinciden con
el ejecutable host validado. El manifiesto liga el binario copiado al SHA256
que esptool cargó. Registros y fuentes: `firmware-manifest.json` y
`firmware-0.6.0/SOURCE-SHA256.json`.

Moonraker real:4 G-codes,20 macros públicas, PNG300 válido de3539bytes.
El archivo multicolor de Orca2.4.2 tiene8 posiciones y pesos
`[1.49,2.06,1.6,0,0,0,0,0.48]`: el cliente publicó4 activos
(T0,T1,T2,T7) y ocultóT3–T6. El JSON real también contiene ejemplos mono.

Ajustes antes/después: host192.168.0.235:7125, brillo59%, reposo1min,
sonido activado, volumen63%. La inspección no añadió voces. Al finalizar,
Klipper seguía conectado en standby, objetivos de boquilla/cama0 y COM8 cerrado.
Las pruebas reales enviaron **cero acciones de impresora y cero escrituras
de ajustes**. El simulador de loopback fue validado por separado; su self-test
comprueba el fixture y no equivale a una impresión real.

## Incidencia encontrada y corregida

La primera prueba en placa encontró reentrancia al cerrar la ventana de red
durante una solicitud de inspección. El callback de teclado recibía DELETE,
evaluaba el guard y volvía a cerrar objetos parcialmente eliminados.

Se corrigió filtrando READY/CANCEL antes del guard y haciendo el cierre
reentrante seguro: retirar punteros globales, desvincular teclado, limpiar
contraseña y eliminar usando referencias locales todavía válidas. Se añadieron
siete escenarios de teclado/inspección/destino/UNKNOWN y la placa volvió a
pasar los38 controles. Se conservan firmware/log iniciales en `first-attempt/`.
La herramienta adicional tuvo un ajuste de parsing del prompt serial; el
registro anterior está separado de los resultados finales aprobados.

## Alcance pendiente de observación

La revisión visual y la precisión táctil del LCD físico requieren observación
del usuario. La política de timelapse está probada con reloj y estados
simulados; falta observarla durante una impresión real. No se inició una
impresión para validar estos cambios.

El resumen del último trabajo se mantiene en RAM, separado del estado activo;
se conserva tras Limpiar estado, pero no se recupera del historial de Moonraker
después de reiniciar el panel o cambiar de servidor. Las heurísticas de
timelapse tienen límite y no identifican cualquier macro arbitraria.

## Artefactos

`firmware-0.6.0/` contiene aplicación/ELF, binarios auxiliares, configuración,
lock y hashes. `application-before.bin` es el respaldo de la partición de
aplicación previa (4MiB, dirección0x10000); no contiene la partición NVS.
`source-before.zip` conserva las fuentes anteriores a esta implementación.

Repetir inspección:

```powershell
python tools/verify_screen_inspection.py --port COM8 --reset --expected-version 0.6.0-portal-controls --output verification/ui-update/device-inspection.log
python tools/verify_ui_update_device.py --port COM8 --before verification/ui-update/before-device.json --output verification/ui-update/device-metadata.log
```

Cerrar previamente cualquier monitor serie. Estas herramientas consultan
estados y metadatos, conservan ajustes y no envían acciones a la impresora.
