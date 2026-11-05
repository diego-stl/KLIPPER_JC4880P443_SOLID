# Firmware de prueba R90 / R270

Versión: `0.1.0-orientation`. Fecha: 2026-10-24, America/Bogota.

**Etapa aceptada:** el usuario confirmó imagen/táctil en ambos giros y eligió R90,
con USB posterior izquierdo. Ver `verification/orientation/aceptacion-r90.md`.
La raíz ahora compila PORTAL0.2.0; los comandos de compilación de esta nota
describen la etapa anterior. Para volver al binario de prueba usar
`tools/restore_orientation.ps1`, que comprueba sus hashes antes de cargarlo.

Proyecto ESP-IDF independiente. Reutiliza la inicialización física de la base validada:
ST7701S, panel nativo 480 × 800, GPIO5/23, LDO3, DSI y dos buffers de salida.
LVGL 9.3.0 dibuja un cuadro lógico de 800 × 480 en un tercer buffer PSRAM.
La misma orientación seleccionada transforma la imagen y los puntos raw del GT911.
Las copias se realizan en bloques de 16 × 16 y se presentan con la sincronización
DMA existente. No se modifica la configuración del panel para conseguir horizontal.

No inicia Wi-Fi/C6, no integra Klipper/Moonraker y no acciona audio ni LED.
No inicializa/escribe NVS. La selección comienza en R90 en cada reinicio.
Reset/INT del GT911 permanecen sin configurar. CPU pre-v3 a 360 MHz y PSRAM a 200 MHz.

## Uso

En pantalla aparecen nueve dianas: cuatro esquinas interiores, centro y puntos
medios de los bordes. El marcador magenta debe seguir al dedo. RAW muestra las
coordenadas nativas 480 × 800; UI muestra el resultado dentro de 800 × 480.
La franja naranja identifica el borde nativo Y=0, sin atribuirle conectores.

- Botones R90 / R270: cambiar montaje horizontal.
- SUMAR +1: debe incrementar una vez al pulsar y soltar, sin repetirse al mantener.
- Las dianas tocadas se rellenan en su color y generan `TARGET_CLICK` por serie.
- Los contadores y dianas se conservan por separado en ambos modos, solo en RAM.

Cada cambio de orientación cancela la interacción anterior y bloquea acciones hasta
presentar un cuadro nuevo y recibir después una liberación GT911 fresca y válida. Si la
pantalla sigue bloqueada aunque ya no haya dedo, **tocar y soltar una vez para
rearmarla**. Ese primer ciclo no cuenta como pulsación. Una ausencia de reporte
no se confunde con soltar. No se desbloquea automáticamente por temporizador.

La consola USB Serial/JTAG a 115200 acepta:

| Comando + Enter | Acción |
| --- | --- |
| `90` / `r90` | Pedir R90 |
| `270` / `r270` | Pedir R270 |
| `status` | Giro, bloqueo, última muestra y contadores de ambos modos |
| `clear` | Borrar contadores SUMAR y marcas de dianas; no escribe almacenamiento |
| `help` | Mostrar comandos |

La consola encola peticiones. Solo la tarea GUI modifica LVGL y la orientación,
entre ciclos de entrada/renderizado. Una lectura fallida no equivale a liberación;
tres errores de entrada deshabilitan el táctil hasta reiniciar, como en la base.

## Comprobación física requerida

1. Elegir R90, colocar la placa con ARRIBA en el borde superior y observar dónde
   quedan USB y los otros conectores. Confirmar imagen completa y colores.
2. Tocar A/B/C/D, el centro y los puntos medios; verificar que marcador y dedo coinciden.
3. Recorrer los bordes y diagonales despacio con un dedo. Comprobar ejes correctos.
4. Pulsar SUMAR una vez; mantener otra pulsación tres segundos y soltar. Debe sumar dos en total.
5. Cambiar a R270 y girar físicamente la placa 180° respecto al montaje anterior.
   Rearmar el táctil si lo pide la pantalla y repetir las comprobaciones.
6. Informar del modo preferido, posición de conectores y cualquier desplazamiento,
   inversión, parpadeo o zona de pantalla sin cubrir.

La prueba automática por serie verifica comandos/cuadros/ausencia de reinicio,
pero no sustituye estas observaciones ni selecciona por sí misma el montaje.

## Compilación y recuperación

Desde la raíz de este proyecto:

```powershell
.\tools\idf.ps1 build
.\tools\idf.ps1 size
.\tools\idf.ps1 flash -Port COM8
.\tools\idf.ps1 monitor -Port COM8
.\tools\test_orientation.ps1
```

Salir del monitor con Ctrl+]. Cerrar el monitor antes de capturas automáticas.
La tabla de particiones conserva la geometría de la variante Wi-Fi validada:
NVS 0x9000/0x6000, phy_init 0xF000/0x1000 y factory 1500 KiB en 0x10000.
Una carga normal escribe bootloader, tabla y aplicación; no borra toda la Flash,
no escribe eFuses ni firmware C6. La NVS existente se conserva, sin leerla.

La recuperación anterior está en el proyecto hermano `JC4880P443 BASE`,
`verification/wifi/d1-accepted/`: binarios, configuraciones y SHA-256 de 0.5.2.
La base y el diagnóstico no se usan como carpeta de compilación del nuevo firmware.
