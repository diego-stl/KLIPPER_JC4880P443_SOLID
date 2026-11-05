# Propuesta gráfica v2 aprobada

`lamina-iconos-logos-v2.png` es la propuesta vigente. Reúne32 iconos y los dos
logos aportados por el usuario. El resto usa UIcons Solid Rounded; se sustituyen
los números13 y14 por los recursos exactos elegidos por el usuario:

-13 Boquilla: https://www.magnific.com/es/icono/alfiler_1518427
-14 Cama: https://www.magnific.com/es/icono/caliente_952538

Los PNG originales permanecen sin modificar en `sources/`; la lámina aplica
el color de la interfaz con una máscara CSS sobre su alfa. Ambos ya tienen
transparencia. `sources/replacements.json` contiene sus URL de descarga.
`lamina-iconos-logos.png` conserva la primera propuesta.
Principales64px; secundarios32px. El ejemplo inferior muestra un botón
208×124px a escala1:1.

El usuario aprobó explícitamente esta versión para integrarla y cargarla en la
placa. Los recursos se integran en **0.6.2-approved-artwork**; la lámina conserva
su aviso histórico de propuesta. El contrato de exportación está en
`../../docs/assets-portal.md` y las evidencias en `../../verification/ui-artwork-update/`.

Fuentes: paquete oficial https://github.com/freepik-company/flaticon-uicons,
revisión fijada en `sources-manifest.json`. La selección con nombres/códigos
está en `selection.json`; la licencia y los recursos de revisión están en
`sources/`. Crédito de la propuesta: Uicons by Flaticon,
https://www.flaticon.com/uicons. El buscador Magnific proporcionado no fue
accesible mediante la herramienta web; se obtuvo la familia oficial UIcons.

Klipper ya tenía transparencia; `logos/klipper-transparente.png` conserva
exactamente los bytes del PNG aportado. Aperture se procesó con la herramienta
integrada ImageGen para retirar el exterior claro; se preservan como parte del
logo las aspas cian, separadores negros y centro oscuro. Se usó la segunda
variante de recorte. No se usó el flujo CLI/API de generación.

Prompt final de Aperture:

> Exact logo background removal, not a redesign. Return the supplied Aperture
> logo as a flat two-color transparent PNG. Remove only the #F7F7F7 exterior
> background. Keep all original blade geometry, rotation, shape and proportions
> exactly. The entire black circular outer border, black central aperture and
> black dividers are solid #111111 and must remain opaque. The cyan blades are
> solid #17C0E9 exactly. Flat uniform fills only: NO gradients, NO shading, NO
> lighting, NO texture, NO 3D, NO glow. Preserve the source raster/logo appearance
> and crisp antialiased boundaries. True transparent alpha outside the circular
> border. No text or added marks. We are preserving an existing brand asset,
> not generating new artwork.

Reproducir la lámina con los recursos locales:

```powershell
node tools/render_icon_review.cjs lamina-iconos-logos-v2
```

El render usa un navegador headless con HTML/fuente/PNG locales. No modifica
los gráficos de `components/portal_assets` ni los originales del usuario.
