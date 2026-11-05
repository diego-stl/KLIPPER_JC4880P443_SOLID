# Prueba de orientación — 2026-10-24, America/Bogota

Firmware `0.1.0-orientation`, ESP-IDF 5.5.6, LVGL 9.3.0. Cargado en COM8,
P4 v1.3, MAC `80:f1:b2:d1:7f:0b`. Aplicación: 658.048 bytes; 57 % libre en factory.

## Comprobaciones realizadas

- Regresión C nativa de las funciones usadas por el firmware: 768.000 puntos raw,
  ocho cuadros RGB565, límites/guardas y bloqueo hasta liberación posterior al cuadro.
- Compilación final correcta; corregido el aviso de const del instalador USB.
- Tabla generada idéntica a los 3072 bytes iniciales de la tabla leída en placa.
  Se conserva NVS en 0x9000/0x6000; no se leyó ni escribió su contenido.
- Bootloader, tabla y aplicación escritos y verificados por hash de esptool.
- Arranque, PSRAM32 MiB a200 MHz, CPU360 MHz, panel y GT911 presentes.
- Trece comprobaciones automáticas de consola/cuadros: correctas.
- Cuadros R90/R270 presentados; rechazo de180 y repetición de un modo comprobados.
- Captura de60 segundos de pulsaciones reales sin reiniciar ni imponer un giro.
- 58 muestras RAW/UI del intervalo capturado coinciden con las transformaciones.
- Eventos de pulsación y liberación observados en ambos modos. R270 registra
  las nueve dianas; el estado posterior indica máscara R90 `0x1fe` y R270 `0x1ff`.
  Falta registrar A en R90. Una diana usada para rearmar no cuenta como clic.
- Una pulsación R90 entre85.082 y88.142 ms (3.060 ms) registró exactamente un
  `COUNT_CLICK`, al soltar: total13. No se interpreta el registro periódico DOWN
  durante ese intervalo como pulsaciones nuevas.
- Estado consultado después: R90, sin bloqueo, sin dedo pulsado. COM8 cerrado.
- En la captura no se observaron pánicos, timeouts DMA ni deshabilitación del táctil.
- Máximos de algunas ventanas: giro ~24,8 ms, presentación ~31–33 ms y handler
  LVGL ~86–93 ms, incluyendo su camino de flush. No son latencia física dedo–píxel.
  PSRAM libre31.247.044 bytes y pila libre de la tarea GUI5.012 bytes en las muestras.
- 48 archivos de la base comparados por SHA-256: sin cambios.

## Pendiente

La aceptación **visual** y la correspondencia del dedo con el marcador requieren
la confirmación del usuario. También falta elegir el montaje y describir la
ubicación real de los conectores. Los eventos recibidos no sustituyen esa observación.
Klipper, Wi-Fi, audio y LED no están integrados en este firmware de prueba.

## Evidencias

- `00-device-identity.log`, `00-partition-read.log`, `partition-before.bin`.
- `01-c-regression.log`, `02-build-final.log`, `03-flash.log`.
- `04-runtime.log/json`: comandos de diagnóstico, sin toques sintéticos.
- `05-user-touch.log`, `05-user-touch-summary.json`: intervalo manual real.
- `06-current-status.log`: consulta sin cambiar el montaje.
- `base-preservation-before.json`, `base-preservation-after.json`.
- `firmware-0.1.0/`: binarios, configuración, lock y SHA-256.

El primer intento de compilación dentro del aislamiento quedó detenido al iniciar
Python; se interrumpió exclusivamente ese proceso y se compiló con el entorno
local existente fuera del aislamiento. El log final corresponde al código cargado.
