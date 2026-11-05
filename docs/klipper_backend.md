# Backend Moonraker

`components/klipper_client` es un componente ESP-IDF 5.5.6, con fachada C para la UI. Usa `esp_http_client`, cJSON de IDF y el componente oficial `espressif/esp_websocket_client ==1.6.1`. No incluye Arduino, Links2004 ni ArduinoJson. No define host por defecto: lee host, puerto, generación e IP disponible del snapshot de `device_network`.

## Comportamiento

La conexión WebSocket hace bootstrap con `server.info`, obtiene información del fallo con `printer.info` si Klipper aún no está listo y suscribe temperaturas, print_stats, virtual_sdcard, webhooks y toolhead. `available` solo se vuelve verdadero después de una instantánea válida con webhooks en ready. Procesa deltas conservando campos omitidos y respetando cero; ordena por eventtime; admite hasta ocho deltas antes de la instantánea. Maneja notificaciones ready/shutdown/disconnected y vuelve a sincronizar. Los mensajes de texto están limitados a 32768 bytes y los fragmentos a cinco segundos; las respuestas RPC tienen plazo de diez segundos. La biblioteca gestiona ping/pong y reconexión. No envía Origin de navegador.

`progress` de la fachada es porcentaje 0..100. Toolhead publica posición, homing y límites reales de los ejes cuando están disponibles, sin suponer cama de 220 mm. La UI debe marcar sus datos como no disponibles cuando `available` es falso.

Las órdenes HTTP tienen cola normal de veinte y resultados Pending/Completed/Rejected/Unknown. Una respuesta perdida o JSON incompleto no acredita que la orden no se haya ejecutado: produce Unknown y detiene el resto del lote normal. No se reintentan POST. FIRMWARE_RESTART puede enviarse aunque Klipper esté en error. La parada de emergencia vacía la cola normal y bloquea nuevas órdenes normales hasta resolver su solicitud; emergencia y apagado tienen dos trabajadores prioritarios independientes, para que un apagado en espera no retrase una parada. Tampoco requieren ready de Klipper.

El cambio de endpoint cancela lo que aún no se ha enviado y publica una sola incertidumbre para las órdenes en curso del host anterior. Sus respuestas posteriores se descartan. Las consultas y el WebSocket también verifican la generación antes de publicar. La desconexión WiFi invalida disponibilidad y evita el envío de nuevas órdenes.

Un trabajador comparte las consultas de lectura: archivos (los quince más recientes), macros (veinte públicas), objetos excluibles (dieciséis), metadatos y descubrimiento opcional de timelapse. La selección de metadatos usa identificadores para descartar respuestas antiguas; los metadatos del trabajo y la caché de miniaturas de lista son independientes. Las lecturas HTTP tienen límites de bytes y plazo total; se rechazan redirecciones. Desde0.6 se prefiere PNG300 con fallback100, hasta128KiB comprimidos para detalles; la lista usa PNG100 hasta20KB por archivo. Se comparan dimensiones declaradas/IHDR y se verifica el framing; LVGL comprueba también la decodificación. La UI posee sus copias y descarta la caché de imagen antes de reemplazarlas. Contrato ampliado: [cliente](../components/klipper_client/README.md).

Los helpers de archivo/macro/objeto validan nombres antes de construir scripts. Las consultas de archivos y objetos no truncan silenciosamente nombres que luego puedan producir órdenes para otro identificador. El envío G-code libre conserva el contrato de consola: acepta scripts multilínea hasta 4096 bytes.

La UI utiliza `klipper_client_action(action, value, expected_generation)` y los valores `K_ACTION_GCODE`, `K_ACTION_EMERGENCY`, `K_ACTION_SHUTDOWN`, `K_ACTION_PRINT_FILE`, `K_ACTION_MACRO`, `K_ACTION_EXCLUDE` y `K_ACTION_RESET_JOB`. La generación pertenece a la pantalla o confirmación que vio el usuario. Enqueue la compara bajo el mutex de datos antes de crear la orden; las APIs anteriores se conservan para compatibilidad. Una instantánea antigua nunca debe autorizar una acción sobre un host nuevo. RESET añade un token de trabajo y revalidaciones de estado antes de transmitir.

El mutex de datos protege únicamente copias, listas y cambios de estado. `snapshot` y `sync_endpoint` no adquieren el mutex `protocol`. Los envíos bloqueantes HTTP/WebSocket se ejecutan fuera del mutex de datos; `protocol` se limita a los trabajadores y callbacks del socket. Por ello una respuesta RPC lenta no bloquea normalmente la lectura de snapshots desde LVGL. El cambio de endpoint vuelve a leer la configuración dentro del mutex para impedir que una lectura anterior restablezca una generación vieja.

Los números recibidos se validan en double antes de convertirlos a float: coordenadas dentro de ±1.000.000 mm, peso/longitud entre cero y 1e9, duración estimada hasta UINT32_MAX. La duración publicada se limita además a 4.294.967.040 segundos, el último float inferior a UINT32_MAX, para que su posterior conversión a entero permanezca definida. Números como 1e300, NaN o infinitos no generan límites de cama infinitos ni conversiones fuera de rango.

## Origen de las reglas y comprobaciones

`private/MoonrakerStatus.h`, `CommandStatus.h` y el origen de `KlipperTransfer.h` proceden de `KLIPPER_ES3C28P_SOLID/src/services`. La versión0.6 amplía la validación PNG y añade `JobPolicy.h` y `MetadataRules.h`, usados por producción y regresiones. `snapshot.state` es el estado filtrado compartido por UI/audio; `raw_state` y `pause_pending` se conservan para validar las acciones. Los transportes y la sincronización son implementaciones para IDF.

Las regresiones `tests/klipper_status_regression.cpp` y `tests/klipper_reliability_regression.cpp` reutilizan las 27 comprobaciones constexpr del origen contra esos archivos de producción. Se ejecutaron correctamente con el compilador RISC-V 14.2.0, C++20, `-Wall -Wextra -Werror -fsyntax-only`. Comprueban reglas puras; no prueban sockets, autorización, LVGL ni hardware.

`tests/klipper_batch_regression.cpp` añade diez comprobaciones de la finalización compartida que usa el trabajador HTTP: ante pérdida de WiFi antes del envío o resultado incierto durante el POST, los veinte sucesores normales se descartan y no vuelven a ejecutarse al reconectar. `tests/klipper_numeric_regression.cpp` añade cuatro comprobaciones de límites geométricos, números fuera de float, datos no finitos y conversión segura de segundos. Las 41 comprobaciones y la sintaxis de `klipper_client.cpp` con las cabeceras/flags reales de IDF5.5.6 y WebSocket1.6.1 se verificaron correctamente.

`tests/klipper_simulator.py --self-test` verificó un servidor local HTTP/WS con la biblioteca estándar de Python. El fixture proporciona instantáneas, deltas, PNG y errores controlados; su propio test no demuestra que el transporte del firmware se haya ejecutado. No se conectó a ninguna impresora ni se flasheó ningún dispositivo durante estas comprobaciones.

## Simulador

```powershell
python tests/klipper_simulator.py --self-test
python tests/klipper_simulator.py --port 17125
```

Por defecto solo escucha en 127.0.0.1. Para pruebas posteriores desde la placa en una LAN se requiere `--bind 0.0.0.0` y configurar expresamente el host/puerto del ordenador en la consola del panel. El simulador nunca establece conexiones hacia impresoras.

`POST /__test/control` recibe un objeto JSON con cualquiera de estas opciones:

| Campo | Efecto |
| --- | --- |
| state | ready, shutdown, disconnected o startup; envía notificación de Klipper cuando procede |
| job_state, filename, progress | Cambia la instantánea del trabajo |
| delta | Envía solo los campos suministrados en un notify_status_update |
| fragment_ws | Divide los mensajes JSON-RPC en dos frames de texto/continuación |
| disconnect_ws | Cierra conexiones WebSocket para comprobar recuperación |
| delay_post | Retrasa la respuesta POST ese número de segundos |
| drop_post | Registra la orden recibida y cierra sin confirmarla |
| reject_post | HTTP 400 con error Moonraker |
| malformed_post | HTTP 200 con JSON inválido |
| truncated_png | Anuncia el tamaño completo y corta el PNG |

`GET /__test/requests` devuelve la bitácora de órdenes recibidas. Para comprobar ausencia de repetición tras incertidumbre, activar drop_post, enviar una orden y revisar que solo exista una entrada. Para generación, usar dos instancias con puertos distintos, retrasar la primera respuesta y cambiar el endpoint durante la petición; el resultado antiguo nunca debe convertirse en éxito del nuevo host.

La compilación global, pruebas del cliente con estos fallos y respuesta física del dispositivo deben registrarse por separado. La autenticación API key/TLS no se incorpora a esta versión; Moonraker debe autorizar al panel en la red configurada.

Fuentes oficiales consultadas: [API WebSocket](https://docs.espressif.com/projects/esp-protocols/esp_websocket_client/docs/latest/index.html), [cabecera exacta 1.6.1](https://github.com/espressif/esp-protocols/blob/websocket-v1.6.1/components/esp_websocket_client/include/esp_websocket_client.h), [compatibilidad del redirect con IDF 5.5.1 y posteriores](https://github.com/espressif/esp-protocols/issues/1059). La cabecera local de IDF5.5.6 contiene `esp_transport_ws_get_redir_uri`.
