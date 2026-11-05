# Consola del panel y prueba de integración controlada

El componente `panel_console` expone `bool panel_console_command(const char*)`. El programa principal debe registrarlo como handler de la consola de `device_network` e incluir el componente en su dependencia CMake. No llama LVGL ni consume la cola de resultados, que sigue perteneciendo a la UI.

Comandos de lectura disponibles:

- `panel status`: estado, disponibilidad, WebSocket, instantánea inicial, generación, temperaturas, progreso, duración, archivos/macros/objetos, metadatos y dimensiones del PNG. Los snapshots grandes están en heap.
- `panel files`: solicita y muestra la lista asíncrona de archivos.
- `panel macros`: solicita y muestra macros.
- `panel objects`: solicita y muestra objetos, exclusiones y límites disponibles.
- `panel metadata`: solicita metadatos del primer archivo de la lista conservando exactamente su nombre. Ejecutar antes `panel files`. No recibe un nombre de archivo porque la consola de red normaliza los comandos a minúsculas.

Las lecturas asíncronas y las pruebas utilizan un único worker de 8192 bytes, con protección de ocupación. El cambio de endpoint aborta la operación. `panel status` no espera HTTP.

## Pruebas activas, exclusivamente contra el simulador

`panel test normal` (también `panel test`) y `panel test uncertain` se omiten del help normal. Antes de enviar cualquier orden, el worker exige:

1. Red con IP, endpoint configurado y Klipper disponible.
2. Puerto exactamente 17125.
3. `GET /__test/identity` con HTTP200, JSON completo, `service` exactamente `JC4880P443_MOONRAKER_SIMULATOR` y booleano `test_only=true`.
4. Host, puerto y generación iguales a los capturados al empezar; todas las acciones utilizan `klipper_client_action(..., expected_generation)`.

Una identidad ausente o incorrecta produce `PANEL_TEST_REFUSED`, sin enviar G-code, emergencia ni apagado. El endpoint de identidad se incorporó a `tests/klipper_simulator.py`. Este simulador nunca conecta a una impresora.

La prueba normal primero lee archivos, macros, objetos y metadatos. Luego encola, con pausas de 500ms: G28, M104 S170, M140 S55, PAUSE, RESUME, primera macro, excluir primer objeto, imprimir primer archivo, emergencia y apagado. Los POST del fixture no actúan sobre hardware. El worker registra aceptación en `PANEL_TEST_ACTION`; que una orden entre en cola no acredita que el transporte la haya completado. Comparar los resultados que muestra/registra la UI con `GET /__test/requests`.

Antes de `panel test uncertain`, configurar el fixture desde el ordenador con `POST /__test/control` y cuerpo `{"delay_post":1,"drop_post":true}`. La demora permite encolar dos scripts consecutivos antes del fallo deliberado de la primera respuesta. El servidor debe registrar `M118 PANEL_TEST_UNCERTAIN_FIRST` exactamente una vez y ninguna entrada de `M118 PANEL_TEST_UNCERTAIN_SECOND`. La UI debe recibir Unknown para la primera y Rejected para la segunda. Después restaurar el fixture con `{"delay_post":0,"drop_post":false}`. No hay transacción de dos llamadas de la API: la prueba usa explícitamente una demora controlada del mock.

El controlador no extrae resultados mediante pop_result. Las comprobaciones del fixture y del worker deben distinguir aceptación de cola, recepción de la petición en el servidor y resultado mostrado por la UI.

Se verificó la sintaxis de `panel_console.c` con GCC RISC-V, cabeceras/configuración reales ESP-IDF5.5.6 y flags de producción; el self-test del fixture también pasó después de incorporar su identidad. Estas comprobaciones no sustituyen la prueba en dispositivo ni afirman que se haya realizado una carga desde este componente.
