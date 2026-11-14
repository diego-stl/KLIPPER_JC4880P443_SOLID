# Recursos Portal para LVGL 9.3

La selección base de 32 iconos proviene de la lámina aprobada
`no nesesario/design/revision-0.6.1/lamina-iconos-logos-v2.png`.
La v3 reemplaza las máscaras de impresión, movimiento, ajustes, parada,
boquilla y HOME. Los botones tienen iconos a escala según el espacio disponible.

## Fuentes y reproducción

`design/revision-0.6.1/selection.json` contiene la selección aprobada. La fuente
es UIcons **Solid Rounded** de Freepik/Flaticon, fijada en `sources-manifest.json`.
La cama usa el PNG **952538**. El crédito de la selección base corresponde a
**Uicons by Flaticon | www.flaticon.com/uicons**. [Licencia de Uicons](../design/revision-0.6.1/sources/LICENSE.txt)

Las máscaras extraídas del PowerPoint v3 se almacenan en `assets/portal/icons-v3/`.
El generador las normaliza a PNG de 64 × 64 px y LVGL aplica el tinte.
El icono de impresión combina una boquilla y un cubo de Pictogrammers.
[Fuentes y hashes](../assets/portal/icons-v3/manifest.json) · [Apache 2.0](../assets/portal/icons-v3/LICENSE-Apache-2.0.txt) · [ISC/MIT de Lucide y Feather](../assets/portal/icons-v3/LICENSE-Lucide.txt)

`tools/export_approved_icons.cjs` exporta los glifos y máscaras locales aprobados
a PNG blancos transparentes de 64 px, sin red. Se guardan en
`assets/portal/approved-icons/`, con hashes de selección, fuente y PNG.
El empaquetador comprueba estos hashes antes de generar el código.

```powershell
node tools/export_approved_icons.cjs
python tools/generate_portal_assets.py
python tools/generate_portal_assets.py --check
```

La exportación inicial requiere Node/Playwright y Edge. Las regeneraciones y
comprobaciones posteriores solo necesitan Python y Pillow 13.0.0.
`assets/portal/contact-sheet.png` muestra los recursos empaquetados. Las fuentes
y los PNG no se enlazan al firmware.

## Logos y personajes

Los logos aprobados se conservan en `design/revision-0.6.1/logos/`. Klipper usa
el PNG transparente proporcionado, sin recolorearlo ni redibujarlo. Aperture usa
el recorte transparente aprobado. Se ajustan manteniendo proporción a canvases
**100 × 80** y **118 × 118**, respectivamente.

GLaDOS y potatOS conservan originales y algoritmo de exportación anterior en
`assets/portal/originals/`. La lente mantiene centro **(152,145)** y radio **7**,
exportados como `PORTAL_EYE_*`. Los cores comparten nueve poses por expresión
y actualizan el visor mientras están visibles. Aperture acompaña el estado
**Sin vista previa** cuando falta una miniatura de G-code válida.

## Contrato LVGL

`portal_assets.h` exporta `portal_icon_<nombre>`, `PORTAL_ICON_SIZE=64` y
`portal_icon_for_symbol()`, con los aliases LV_SYMBOL previos. Los nuevos nombres
son `last_job`, `audio`, `mute`, `cool` y `motors_off`. NULL/desconocido devuelve NULL.
Los iconos admiten recoloración con LV_OPA_COVER. Escala 128 produce 32 px y 256 produce 64 px;
el pivote (0,0) permite reservar y alinear el tamaño visual. Logos y personajes
conservan sus colores.

RGB565 little endian seguido del plano A8 completo, `LV_COLOR_FORMAT_RGB565A8`,
stride RGB=ancho×2, sin alfa premultiplicada. Datos y descriptores const alineados
a 4 bytes en flash;
sin decodificación PNG ni asignación de heap para estos recursos.

| Recurso | Canvas | Bytes de píxeles |
| --- | --- | ---: |
| GLaDOS | 288 × 248 | 214272 |
| potatOS | 248 × 240 | 178560 |
| Aperture | 118 × 118 | 41772 |
| Klipper | 100 × 80 | 24000 |
| 32 iconos | 64 × 64 | 393216 |
| Total generado | | 851820 |

El enlazador descarta recursos sin referencias. Se generan 206592 bytes de píxeles
más que con los 27 iconos procedurales de 48 px; la compilación mide el coste real.
