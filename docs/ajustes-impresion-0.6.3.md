# Ajustes de impresión y navegación — 0.6.3

Cambios aprobados el 2026-10-27, America/Bogota. Se conservan los iconos y logos
aceptados de la versión 0.6.2.

## Pantallas de impresión y pausa

El botón **AJUSTES IMPRESION** sustituye a Toolhead en ambas pantallas. Mantiene
208 × 124 px y el icono de Ajustes de 64 px. El panel conserva seis botones.

Abre un panel con tres filas independientes:

| Ajuste | Rango propuesto | Orden al aplicar |
| --- | --- | --- |
| Velocidad | 10–200 % | M220 |
| Flujo | 50–150 % | M221 |
| Ventilador de capa | 0–100 % | M106 |

Cada fila distingue **ACTUAL**, recibido de Klipper, y **PROPUESTO**, preparado
con los botones −/+ en pasos de 5 puntos porcentuales. **APLICAR** envía solo
esa fila. Abrir, editar el borrador o cerrar no envía órdenes. Un dato ausente
se muestra como `-- %` y sus controles quedan deshabilitados. El valor actual
permanece separado de los límites aplicables al borrador.

Velocidad y flujo proceden de `gcode_move.speed_factor` y `extrude_factor`:
1,0 equivale al 100 %. El ventilador procede de `fan.speed`, entre 0 y 1,
y se convierte a S0–255 al aplicar. Se siguen los contratos oficiales de
[estado](https://www.klipper3d.org/Status_Reference.html#gcode_move) y
[órdenes G-code](https://www.klipper3d.org/G-Codes.html#g-code-commands).

La suscripción tolera un ventilador estándar ausente. No se inventan valores
predeterminados; los deltas parciales conservan los campos conocidos y los
datos se invalidan al desconectar o cambiar de destino.

El panel liga los ajustes a generación del servidor, trabajo, archivo y estado.
Se cierra si cambia su contexto. El cliente vuelve a verificar destino, trabajo,
estado, época de conexión y disponibilidad de la lectura al encolar y antes del
cuerpo HTTP. Las respuestas inciertas siguen sin reintento automático.

El acceso a Toolhead, movimientos XY/Z, homing y motores OFF queda bloqueado
durante impresión, pausa o inicio pendiente. Si una impresión externa empieza
con Toolhead abierto, se vuelve a la vista de estado. Extruir y retraer de la
pantalla de pausa conservan su función.

## Inicio desde G-codes

Al aceptar la cola de **IMPRIMIR**, se abre inmediatamente la pantalla de estado.
Mientras Klipper confirma el inicio se muestra **Iniciando impresion...**.
No se simula el estado printing: la vista sigue el estado real recibido por
WebSocket. Una respuesta HTTP aceptada tampoco se toma como confirmación de
impresión. El bloqueo de doble envío permanece hasta el estado correspondiente
o rechazo confirmado. Un resultado incierto se mantiene visible sin reenvío.

El seguimiento reconoce también un trabajo nuevo que ya llegó a completado o
cancelado entre dos actualizaciones de la interfaz. No confunde un resultado
terminal anterior con el inicio solicitado.

## Verificación

Los escenarios de escritorio ejecutan LVGL y callbacks productivos, con servicios
simulados. Incluyen navegación inmediata y posterior, doble toque, rechazo y
UNKNOWN, ausencia de Toolhead, bloqueo de movimientos, borradores sin envío,
aplicación individual, límites, datos inválidos y cambio de contexto.

La regresión C++ usa las reglas productivas de conversión, deltas y guardas.
La verificación en placa compara `PANEL_TUNING` con consultas GET a Moonraker,
conserva ajustes y no envía órdenes de impresión ni de ajuste a la impresora.
Las evidencias de compilación, carga y resultados están en
`../verification/print-settings-update/`.
