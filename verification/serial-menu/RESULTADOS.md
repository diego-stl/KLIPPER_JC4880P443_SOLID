# Navegación serial: carga y verificación

2026-10-26, America/Bogota. Firmware cargado en la Guition JC4880P443 de COM8,
identidad USB 303A:1001 / 80:F1:B2:D1:7F:0B. Esptool confirmó ESP32-P4 v1.3 y
verificó el hash de bootloader, aplicación y tabla de particiones.
Se utilizó `tools/idf.ps1 flash -Port COM8`, con ESP-IDF 5.5.6.

La verificación final pasó los 39 controles registrados en `03-device-menu.json`:
arranque de PORTAL, PSRAM, inicialización del táctil y audio, los siete menús,
opciones numeradas, retorno al principal, comandos completos, ayuda, CR/CRLF,
cancelación con Esc, entrada vacía e inválida y cancelación del borrado de Wi-Fi.
No se enviaron acciones a la impresora ni comandos para modificar ajustes.

Wi-Fi conectado con IP local 192.168.0.194. Moonraker 192.168.0.235:7125 conectado,
Klipper en standby, WebSocket e instantánea inicial disponibles. Audio activo al83%,
brillo59% y reposo1 minuto leídos tras reiniciar. Sin fallos de ejecución observados
durante la captura. La prueba no acredita inspección visual, funcionamiento táctil
manual ni calidad acústica.

La captura inicial `02-device-menu.*` conserva un fallo de sincronización de la
herramienta: el prompt de arranque pendiente satisfizo antes de tiempo la primera
respuesta a M. Se corrigió la captura para consumir ese prompt y mantener controles
con nombres únicos. La segunda captura pasó sin cambiar ni volver a flashear el firmware.

Las opciones identificadas como pendientes siguen mostrando avisos. La sesión
terminó en el menú principal y COM8 quedó cerrado y disponible para el monitor.
`M` + Enter muestra el menú principal.

Evidencias: `01-flash.log`, `03-device-menu.log`, `03-device-menu.json` y
`firmware-manifest.json`, con tamaños y SHA256 de los binarios cargados.
