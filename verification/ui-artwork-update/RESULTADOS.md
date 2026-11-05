# Integración gráfica 0.6.2 — resultados

2026-10-26, America/Bogota. **0.6.2-approved-artwork** cargada y verificada
en COM8, ESP32-P4 v1.3, USB `80:F1:B2:D1:7F:0B`.

## Cambios entregados

- Integrados los **32 iconos y dos logos** de la lámina v2 aprobada por el usuario.
- UIcons Solid Rounded; boquilla **1518427** y cama **952538** elegidas por el usuario.
- Iconos principales de **64 px** y secundarios de **32 px**, con textos descriptivos.
  Botones bajos con icono y texto en horizontal; botón Imprimir de G-codes
  conservado con texto y altura de 48 px.
- Logos PNG transparentes aprobados, ajustados manteniendo proporción.
- GLaDOS conservada en espera/completado/cancelado y fallo; Aperture + NO DATA
  permanece cuando falta la miniatura del G-code.
- Crédito de Flaticon visible en Ajustes. Originales, selección, licencia y hashes
  conservados en el proyecto; exportación determinista sin recursos de red.
- Recursos RGB565A8 residentes en flash, alineados a 4 bytes, sin decodificación
  PNG para iconos/logos ni asignaciones de heap por estos recursos.

## Validación

**115/115 escenarios**, **175 eventos LVGL**, once vistas de 800 × 480,
**0 recortes** de etiquetas y **0 advertencias de alineación**. Se conservaron
los 107 escenarios funcionales anteriores. Los nuevos verifican iconos visibles,
tamaños, fuentes semánticas, separación de textos, logos y crédito. Las capturas
se revisaron contra la lámina v2 y las huellas de fuentes coinciden.

Compilación ESP-IDF 5.5.6 aprobada. **38/38 controles en placa**: versión de
arranque, aplicación de las once vistas, regreso al modo normal, conexión,
ajustes y ausencia de fallos de ejecución. **7/7 controles de metadatos** con
Moonraker real: ajustes conservados, macros públicas, conexión, objetivos a cero,
PNG de 300 × 300, filamentos activos y ausencia de fallos.

Se verificaron cinco G-codes y veinte macros públicas. El archivo más reciente
contiene ocho posiciones con cinco consumos positivos; el panel obtiene el PNG
de 300 px y muestra los cinco colores activos. Se mantiene la lista izquierda
solo con nombres y sin descargas de miniaturas por archivo.

## Firmware y respaldo

Aplicación: **3066576 bytes**, partición de 4 MiB con **27 % libre**.
SHA-256 de la aplicación instalada y verificada por esptool:

```text
5ca10907ac6ca46c5acc5fd08db9ef4180d5aa2de2e07b3633f3629510a1ff27
```

Solo se escribió la aplicación en `0x10000`; NVS no se flasheó. El respaldo
de 4 MiB `application-before.bin` coincide, en el tramo de la aplicación,
con la versión 0.6.1 previamente instalada. Ajustes conservados:
Moonraker `192.168.0.235:7125`, brillo 59 %, reposo 1 min, sonido activado,
volumen 49 %. La placa quedó en modo normal, Klipper en espera, objetivos
térmicos a cero y COM8 cerrado.

La comprobación del LCD físico queda a la observación del usuario. No se inició
una impresión ni se enviaron movimientos, calentamiento, cambios de ajustes
o reinicios de Klipper durante estas pruebas.

Evidencias: `firmware-manifest.json`, `build.log`, `flash-application.log`,
`device-inspection.json`, `device-metadata.json`, `host-action-report.json`,
`asset-check.json`, capturas `qa_artwork_*.png` y paquete `firmware-0.6.2/`.
La especificación de exportación está en `../../docs/assets-portal.md`.
