# PORTAL 0.6.0 — interfaz y configuración táctil

Implementación del plan aprobado el 2026-10-26 (America/Bogota), sobre el
panel nativo LVGL 9.3, 800 × 480, R90. Evidencia reproducible en
`verification/ui-update/` y `tests/portal_host/output/`.

## Pantallas principales

Espera, completado, cancelado, impresión y pausa usan seis botones de
208 × 124 píxeles, columnas x=360/580, filas y=70/207/344. El panel izquierdo
mide 332 × 398; en impresión/pausa contiene archivo, imagen de 200 × 200,
porcentaje y dos tarjetas de temperatura actual/objetivo de 304 × 52.
Se retiraron los contadores restante/transcurrido de esas vistas.

| Estado | Primera fila | Segunda fila | Tercera fila |
| --- | --- | --- | --- |
| Espera/completado/cancelado | Imprimir / Mover | Último trabajo / Ajustes | Macros / Parada |
| Imprimiendo | Pausar / Cancelar | Macros / Excluir | Toolhead / Parada |
| Pausa | Reanudar / Cancelar | Extruir / Retraer | Toolhead / Parada |

Último trabajo abre el resumen terminal con imagen, archivo, resultado,
tiempo estimado, peso/longitud y filamentos activos. El botón Limpiar estado
y volver a espera solicita `SDCARD_RESET_FILE` con el identificador del trabajo
y la generación del servidor. El cliente comprueba ambos y el estado terminal
antes de encolar y transmitir. El resumen se conserva separado de los datos
activos y de la selección de archivos, también después de limpiar. Reside en
RAM del panel; cambiar de servidor o reiniciar el panel no consulta un historial
persistente de Moonraker. Una respuesta incierta nunca provoca un reintento.

## G-codes y miniaturas

Seleccionar actualiza la información; Imprimir inicia una sola solicitud sin
diálogo secundario. El botón se bloquea inmediatamente y se revalidan conexión,
estado y generación. Los nombres y las áreas táctiles de las filas son mayores.

En Orca se recomienda `100x100/PNG,300x300/PNG`. Se ignora 48 × 48.

| Uso | Imagen fuente | Área visible |
| --- | --- | --- |
| Lista | PNG 100 × 100 | 96 × 96 |
| Archivo seleccionado | PNG 300 × 300 | 240 × 240 |
| Impresión/pausa | PNG 300 × 300 | 200 × 200 |
| Detalles | PNG 300 × 300 | 300 × 300 |

El detalle prefiere PNG300, con fallback PNG100 y hasta 128 KiB comprimidos.
Las dimensiones anunciadas se comparan con el PNG. La lista tiene caché
independiente y acotada; su refresco no reemplaza la selección. LVGL valida
también la decodificación. Ante ausencia o fallo se usa Aperture + `NO DATA`;
durante una consulta pendiente aparece `Cargando...`.

## Filamentos

Se conservan los índices originales al asociar `filament_colors`,
`filament_weights` y `referenced_tools`. Se ocultan pesos cero conocidos;
una referencia explícita de herramienta puede conservar un consumo redondeado
a cero como `Uso confirmado`. Un campo ausente permanece `Sin dato`.
Los consumos positivos menores de 0,1 g se representan como `<0.1 g`.
Con una herramienta confirmada puede usarse su peso total. Los datos sin
desglose fiable no se reparten entre colores por suposición.

La lectura real de Orca 2.4.2 aportó ocho colores con pesos
`[1.49,2.06,1.6,0,0,0,0,0.48]`, referencias vacías y total 5,64 g:
se muestran T0, T1, T2 y T7. El ejemplo mono usa un color y 7,5 g.

## Macros y estados

Las macros `_...` quedan ocultas antes del límite de veinte visibles.
Para ocultar otra macro desde la configuración de Klipper se puede añadir
`variable_portal_visible: False` dentro de su sección. El panel interpreta el
campo runtime `portal_visible`; sin marcador mantiene las macros públicas.
Los cambios se leen al volver a abrir Macros. No se modifica automáticamente
la configuración de la impresora.

Una única política de estado alimenta la pantalla y el audio. Una pausa desde
impresión se confirma tras 2500 ms. Si existe una macro compatible
`TIMELAPSE_TAKE_FRAME`, sus indicadores recientes pueden ampliar la espera
hasta quince segundos; al terminar dan un segundo de margen para reanudar.
Un flag atascado vence, y error/cancelación/completado tienen prioridad.
La primera sincronización ya pausada se muestra directamente y es silenciosa.
Son heurísticas acotadas; la aceptación durante timelapse real requiere una
impresión observada. Reiniciar Klipper desde Error es directo.

## Ajustes y gráficos

Arranque y Error dan acceso a Ajustes, que permanece abierto durante errores
y reconexiones. La nueva ventana Wi-Fi/Moonraker permite escanear, elegir o
introducir SSID, contraseña oculta, guardar/conectar, host/IPv4, puerto y prueba
de conexión. Usa el worker de red existente y NVS, con peticiones asíncronas.
La consola y la pantalla comparten validación y guardado. Brillo, reposo y audio
conservan sus ajustes existentes. Véase `components/device_network/README.md`.

Se incorporan 27 iconos semánticos recoloreables, logos revisados y un indicador
Klipper de 14 px. Se preservan los rótulos y la identidad PORTAL. Los recursos
son deterministas: `tools/generate_portal_assets.py --check`.

## Catálogo de inspección

| Opción serial | Nombre | Captura host |
| --- | --- | --- |
| 1 | Arranque | 00_startup.png |
| 2 | Espera/completado/cancelado | 01_standby.png |
| 3 | Imprimiendo | 02_printing.png |
| 4 | Pausa | 03_paused.png |
| 5 | Toolhead | 04_toolhead.png |
| 6 | G-codes | 05_files.png |
| 7 | Macros | 06_macros.png |
| 8 | Excluir objetos | 07_exclude.png |
| 9 | Ajustes | 08_settings.png |
| 10 | Error | 09_error.png |
| 11 | Detalles | 10_details.png |
| 12 | Volver al funcionamiento normal | Acción; no crea una vista |
| 13 | Estado de inspección | Consulta; no crea una vista |

Las opciones 12/13 podían dejar visible la misma pantalla que la opción 10.
No hay tres árboles de pantalla duplicados: son once vistas, con índices
de capturas empezando en cero. La inspección sigue bloqueando controles y
manteniendo resultados rechazados/inciertos hasta su cierre.

## Verificación y límites

El harness ejecuta UI y LVGL reales, con servicios simulados; cubre tamaños,
filamentos, red/teclado, cambios de destino, cierre de ventanas, doble toque,
último trabajo, audio e inspección. No acredita precisión del táctil físico.
Las regresiones C++ evalúan las políticas productivas con reloj simulado;
el self-test HTTP/WS comprueba el fixture y no sustituye el transporte ESP.
La compilación y las pruebas de placa se documentan por separado.

La primera prueba de placa detectó reentrancia al destruir el teclado con un
guard de inspección activo. Se filtraron los eventos del teclado y se protegió
el cierre retirando los punteros globales antes de borrar; se añadieron siete
regresiones de este grupo. La evidencia inicial se conserva en
`verification/ui-update/first-attempt/`.
