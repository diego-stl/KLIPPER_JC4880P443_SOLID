# Restauración de la base 0.6.5

2026-10-28, America/Bogota. Proyecto restaurado, compilado, probado y cargado
en el JC4880 ESP32-P4 v1.3 identificado en COM8, USB `80:F1:B2:D1:7F:0B`.

## Archivos

Se restauraron las fuentes anteriores de la UI, configuración de red, assets,
generador, renderer, pruebas, orientación y archivos CMake. Se regeneraron los
iconos y el Aperture original azul con el exportador anterior. Los componentes,
helpers y manifiestos del tema experimental quedaron fuera de las rutas activas.
Los assets originales proporcionados por el usuario permanecen disponibles.

La auditoría coincide en 214 archivos con el manifiesto 0.6.3. Los otros dos,
`CMakeLists.txt` y `board_display.c`, coinciden exactamente con la copia 0.6.5:
versión 0.6.5 y retroiluminación de 100 Hz. La UI no cambió entre esas versiones.
Ver `source-audit.json` y `restored-source-report.json`.

El trabajo experimental está archivado en `experimental-source.zip` (238 entradas
de proyecto, con hashes), además de las adiciones retiradas en `removed/`.
El binario experimental también se conserva como `application-experimental.bin`.

## Comprobaciones

- Compilación ESP-IDF 5.5.6 correcta: 3.075.040 bytes y 1.119.264 bytes libres en factory.
- 156/156 escenarios funcionales, 533 eventos LVGL.
- Once capturas 800 × 480 sin labels fuera de pantalla; resumen `restored-ui.png`.
- Los bytes de código y assets del binario recompilado son idénticos al firmware
  0.6.5 guardado. Solo cambian fecha/hora, huella ELF y checksum/digest de imagen.
- Esptool validó la integridad de la imagen antes de cargarla.

## Placa

Se leyó y conservó la partición de aplicación de 4 MiB antes de escribir:
`device-application-before.bin`. Su prefijo coincide con el binario 0.6.5 original.
**La placa ya tenía esa versión**, por lo que la recarga conserva su aspecto.
La restauración que sí era necesaria fue la de los archivos del proyecto.

Se cargó `application-restored.bin` únicamente en `0x10000`; esptool confirmó
`Hash of data verified`. No se escribió NVS, bootloader ni tabla de particiones.
No se enviaron órdenes de impresión, movimiento, calentamiento o configuración.

Tras el arranque pasaron ocho comprobaciones: versión, PWM 100 Hz, GUI,
inicialización del táctil, preferencias, conexión a Klipper, códec y ausencia
de fallos de ejecución. Se conservaron brillo 100 %, volumen 89 %, sonido activo,
reposo de 1 minuto y Moonraker 192.168.0.235:7125. COM8 quedó cerrado al terminar.
La revisión visual del LCD físico corresponde a la observación del usuario.

SHA-256 de la aplicación cargada:
`b7e1a4c49c08dfcfc603a1844989a91e1abc04195a0b2ba8e9c6e86d26def962`.

Evidencia: `build.log`, `host-action-report.json`, `flash.log`,
`after-device.log`, `after-device.json` y `firmware-manifest.json`.

La utilidad no interactiva de compilación queda en `build-restored.ps1` dentro
de esta carpeta. La estructura activa del proyecto vuelve a la base anterior.
