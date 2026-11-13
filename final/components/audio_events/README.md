# Avisos por estado real de Klipper

`audio_events_update()` recibe exclusivamente el snapshot real del backend desde el dueño de la GUI, aproximadamente cada 200ms. No consulta ni modifica la impresora. Las llamadas de audio solo encolan trabajo en `audio_service`; la consola `audio events` / `audio events status` es de lectura y debe despacharse antes de la consola genérica `audio_service`.

El primer `audio.ready` consume el aviso de arranque una vez. Si sonido está desactivado o volumen es cero, lo omite definitivamente. Las primeras instantáneas, reconexiones y cambios de endpoint establecen una línea base silenciosa, incluso cuando ya existe una impresión o un fallo. Las transiciones posteriores reproducen printing (incluido reanudar), printing→paused, cancelled, complete, standby y error. El error es válido con socket abierto aunque Klipper no esté disponible ni conserve la suscripción. Startup y offline no hablan.

Los estados siguen avanzando mientras el sonido está apagado o no está listo. Un cambio de destino o la pérdida de la fuente de estados invalida los clips asociados a Klipper mediante `audio_service_stop_printer()`. Los avisos de estado se originan en las instantáneas de Klipper. La interfaz emite sus confirmaciones de acciones a partir de los resultados de las solicitudes.

`AUDIO_EVENTS_CLIP` cuenta solicitudes aceptadas por el servicio, no confirma que hayan llegado al altavoz; contrastar con los contadores de `audio_service` y observación física. `reconnections` cuenta líneas base recuperadas tras una anterior, también si cambió el endpoint; `endpoint_changes` registra específicamente esos cambios.

Las reglas puras están en `audio_events_logic.h`; `tests/audio_events_regression.c` ejecutó correctamente 95 aserciones sobre arranque, mute/volumen0, retraso del códec, impresión preexistente, error inicial, desconexión, endpoint y transiciones rápidas. La sintaxis del componente real se verificó con los flags y cabeceras ESP-IDF5.5.6. Estas pruebas no reproducen audio ni envían órdenes a la impresora.
