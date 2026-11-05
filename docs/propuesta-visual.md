# Propuesta PORTAL · 800 × 480

Fecha: 2026-10-24, America/Bogota. Estado: propuesta para revisión, con datos simulados.

## Decisiones de composición

Se conserva la identidad del proyecto original: fondo `#050B10`, panel `#0C151E`,
bordes `#284B66`, texto `#F5F8FC`, información secundaria `#ABC3DF`, cian `#00C9F4`,
naranja `#FF9D00`, verde `#00E676` y rojo `#FF304D`. Se reutilizan GLaDOS y potatOS.
No se incorpora NERV, ni controles de audio o LED en esta etapa.

El LCD tiene una cabecera de 48 px, márgenes laterales de 12 px y contenido
entre y=70 e y=468. Todas las vistas usan el mismo marco lógico de 800 × 480.
Los textos principales se sitúan entre 16 y 29 px; rótulos secundarios, entre 11 y 14 px.
La fuente del prototipo es Arial; en firmware se elegirá la fuente LVGL disponible
con las métricas verificadas. Los renders HTML/SVG no certifican las métricas de LVGL.

| Vista | Distribución y comportamiento propuestos |
| --- | --- |
| Arranque | Wi-Fi, Aperture y Klipper con diagnóstico central; la transición depende de red y estado válido real en firmware. |
| Espera | Tarjeta GLaDOS/temperaturas a la izquierda y seis acciones a la derecha. |
| Impresión | Archivo, imagen, porcentaje y tiempos a la izquierda; temperaturas y controles a la derecha. Miniatura prioritaria si existe. |
| Pausa | Reanudar, cancelar y extrusión/retracción directas; Toolhead y emergencia accesibles. |
| Toolhead | Boquilla/cama y material a la izquierda; XY, Z, paso y homing a la derecha. Aplicar material es una acción explícita. |
| G-codes | Lista y vista previa simultáneas. Impresión mediante confirmación. |
| Macros | Lista seleccionable y ejecución de la macro seleccionada, sin editor nuevo de parámetros. |
| Exclusión | Mapa y lista vinculados. Cama de 220 × 220 mm solo como ejemplo, pendiente del perfil real. |
| Ajustes | Brillo, reposo, identificación PORTAL y acceso informativo a configuración por consola. |
| Error | Ilustración a la izquierda, mensaje completo/desplazable a la derecha y recuperación debajo. |
| Detalles | Imagen y metadatos con mayor espacio de lectura. |
| Orientación | Marcadores y coordenadas para comparar los dos montajes horizontales. |

## Interacciones del prototipo

Los botones cambian de pantalla, seleccionan archivos/macros/objetos y abren
diálogos de impresión, cancelación, emergencia y recuperación. La edición térmica
usa incrementos de muestra; aplicar/cancelar mantiene la semántica del original.
Materiales y pasos alternan opciones para poder revisar el espacio disponible.
Los avisos indican explícitamente que las acciones están simuladas.

La miniatura de prueba procede del fixture sintético 100 × 100 del original.
No representa una pieza real ni define el tamaño que tendrá la miniatura definitiva.

El visor exterior permite ir directamente a cualquier vista y alternar R90/R270.
Esos controles de revisión no pertenecen al LCD ni se trasladarán al firmware.

## Etapas siguientes

1. Revisar distribución, textos y tamaños de las vistas propuestas.
2. Implementar en el nuevo proyecto un patrón horizontal con R90/R270 y comprobarlo físicamente.
3. Trasladar widgets a LVGL 9 y verificar sus métricas, imágenes y eventos.
4. Integrar la capa Moonraker y la persistencia; revisar tamaño de aplicación y particiones.
5. Comprobar tráfico sostenido, reconexión, resultados de órdenes y operación real.

La base actual tiene pruebas aceptadas en vertical; no se considera validada
la orientación horizontal, el rendimiento del nuevo giro o la aplicación Klipper.
