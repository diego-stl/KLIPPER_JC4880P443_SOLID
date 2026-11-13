# Recursos Portal para LVGL 9.3

Los 32 iconos provienen de la lámina aprobada
`no nesesario/design/revision-0.6.1/lamina-iconos-logos-v2.png`.
Se conservan los textos. Los iconos principales miden **64 × 64 px** y los
secundarios **32 px**. Los botones bajos usan icono y texto en horizontal.

## Fuentes y reproducción

`design/revision-0.6.1/selection.json` contiene la selección aprobada. La fuente
es UIcons **Solid Rounded** de Freepik/Flaticon, fijada en `sources-manifest.json`.
Boquilla y cama usan los PNG elegidos: **1518427** y **952538**. La licencia está
en `design/revision-0.6.1/sources/LICENSE.txt`. Crédito de los iconos:
**Uicons by Flaticon | www.flaticon.com/uicons**.

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
comprobaciones posteriores solo necesitan Python y Pillow 12.3.0.
`assets/portal/contact-sheet.png` muestra los recursos empaquetados. Las fuentes
y los PNG no se enlazan al firmware.

## Logos y personajes

Los logos aprobados se conservan en `design/revision-0.6.1/logos/`. Klipper usa
el PNG transparente proporcionado, sin recolorearlo ni redibujarlo. Aperture usa
el recorte transparente aprobado. Se ajustan manteniendo proporción a canvases
**100 × 80** y **118 × 118**, respectivamente.

GLaDOS y potatOS conservan originales y algoritmo de exportación anterior en
`assets/portal/originals/`. La lente mantiene centro **(152,145)** y radio **7**,
exportados como `PORTAL_EYE_*`. GLaDOS continúa en los estados principales y
Aperture + **NO DATA** cuando falta una vista previa de G-code válida.

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
