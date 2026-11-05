# Orientación horizontal: R90 y R270

Fecha: 2026-10-24, America/Bogota. Convención de la propuesta y del firmware de prueba.

**Resultado:** R90 aceptado por el usuario, USB posterior izquierdo; imagen y
táctil coinciden en ambos modos. Ver `verification/orientation/aceptacion-r90.md`.
Los apartados de validación pendiente describen el procedimiento previo,
no un bloqueo actual de la integración PORTAL.

## Punto de partida comprobado

En `../JC4880P443 BASE` el panel y el GT911 son nativos de 480 × 800.
`components/board_jc4880/include/board_config.h` fija esas dimensiones y giro GUI 180°.
`components/gui_port/gui_port.c` admite exclusivamente 0°/180° mediante una aserción,
crea un display de 480 × 800 y exige un cuadro completo nativo en flush.
`components/gt911_input/gt911_input.c` recibe coordenadas nativas y aplica una sola
transformación de 180°; `gui_touch.c` entrega ese resultado a LVGL.

La aceptación física anterior de pantalla/táctil corresponde a esa posición vertical.
Un modo horizontal de 800 × 480 exige ampliar el puerto, aun cuando el número total
de píxeles y los 768.000 bytes por framebuffer RGB565 sean los mismos.

## Convención exacta

R90/R270 describen el giro **del framebuffer lógico hacia el framebuffer nativo**.
No son una afirmación sobre el ángulo del soporte respecto a una fotografía.
Los nombres tampoco asignan una ubicación a USB u otros conectores.

Sea `(u,v)` la UI 800 × 480 y `(x,y)` el panel/GT911 480 × 800:

| Giro | Imagen UI → panel | Táctil raw → UI |
| --- | --- | --- |
| R90 horario | `x=479-v; y=u` | `u=raw_y; v=479-raw_x` |
| R270 horario | `x=v; y=799-u` | `u=799-raw_y; v=raw_x` |

| Esquina UI | Panel R90 | Panel R270 |
| --- | --- | --- |
| (0,0) | (479,0) | (0,799) |
| (799,0) | (479,799) | (0,0) |
| (0,479) | (0,0) | (479,799) |
| (799,479) | (0,799) | (479,0) |

En el patrón, la marca naranja indica **el borde nativo y=0**. En R90 queda
a la izquierda de la UI; en R270, a la derecha. Se separa esta referencia de la
posición vertical actual, que ya tenía un giro de 180°. Al cambiar de R90 a R270,
la placa ocupa dos posiciones físicas opuestas y la UI vuelve a verse derecha.

Los cuatro blancos de prueba están en UI (24,24), (775,24), (24,455) y (775,455).
Sus rótulos RAW son las coordenadas inversas exactas; el centro elegido (400,240)
corresponde a R90 (239,400) o R270 (240,399).

## Implementación en el firmware de prueba

Aplicada en `0.1.0-orientation`; ver [uso y evidencias](prueba-orientacion.md).

- Separar constantes físicas 480 × 800 de constantes lógicas 800 × 480.
- Mantener tabla ST7701S, timings DSI, pinout y buffers de salida nativos.
- Crear la superficie LVGL lógica y copiar/rotar el cuadro hacia el buffer libre.
- Transformar el GT911 una sola vez desde raw, sin acumular el giro 180° previo.
- Conservar inicialmente las esperas DMA y el orden de arranque GUI/Hosted validados.
- Medir tiempo de giro, presentación y respuesta táctil antes de optimizar.
- Reset/INT desconocidos del GT911 continúan sin configurar.

## Comprobación física pendiente

Probar las dos opciones con texto ARRIBA/ABAJO, marcadores distintos y referencia
de bordes. Identificar conectores en la unidad y elegir el montaje que convenga.
Después comprobar centro, cuatro blancos interiores, mitades y recorrido de bordes;
pulsar, mantener y soltar; verificar liberación y ausencia de repetición involuntaria.
Observar cobertura, colores, parpadeo/cortes y respuesta durante conexión Wi-Fi.

La simulación de navegador permite revisar el patrón y comprobar matemáticamente
las transformaciones. No demuestra orientación, calibración o respuesta física.
El visor original no accede al dispositivo. El firmware de diagnóstico se compiló,
cargó y comprobó por serie como una etapa posterior; la aceptación física se
mantiene separada de la comprobación matemática y de los eventos de cuadros.
