# Device network

ESP-IDF 5.5.6, ESP-Hosted 2.12.13 and esp_wifi_remote 1.6.5, pinned exactly.
Call `device_network_start()` once after the GUI is ready. It loads panel NVS
settings synchronously, then starts a worker which waits four seconds before
initializing Hosted. The project CMake must exclude Hosted's early constructor,
as in BASE. The component checks the validated P4 pre-v3/360 MHz and SDIO1
5 MHz pin configuration at compile time. It neither updates nor flashes the C6.

No LVGL calls occur here. A critical section protects the public snapshot; a
mutex serializes panel NVS updates from the GUI and console. Display settings
are persisted only: the caller controls backlight and idle handling.

WiFi persistence remains binary-compatible with BASE: `jc_wifi/station_v1`.
Passwords are accepted at the masked local console or hidden touch field, never printed, and
temporary credential buffers are explicitly wiped. No password or SSID is part
of the public snapshot. No NVS partition is automatically erased. Panel host,
port, brightness and sleep timeout use `jc_panel/settings_v1`; no endpoint is
hardcoded. Empty host means the user must configure Moonraker.

USB console accepts case-insensitive commands (credential bytes are preserved):

- `M/P/A/W/K/D/L`: main menu, display/touch, audio, WiFi, Klipper endpoint,
  system diagnostics and logs. Letter or number + Enter; `0`/`M` returns to main.
  Empty Enter redraws the current menu; each section has a distinct prompt.
  Shortcuts and full commands work in every section, even without WiFi readiness.
  `P` option `4` opens a nested screen-inspection menu. Its `0` returns to Display;
  that return and all section shortcuts forward `screen live` before redrawing.
- `config` / WiFi option `3`: numbered WiFi scan, network selection and masked password;
  `m` selects a manually entered SSID. ESC cancels.
- `status`: local IP, readiness, endpoint and panel display settings.
- `scan`, `connect`, `reconnect-test`, `check`, `forget` preserve BASE semantics.
  `forget` removes only the station blob; it retains panel settings.
- `moonraker` / `I`: interactive host/IP and port (blank port uses 7125).
- `moonraker set HOST PORT`: noninteractive endpoint configuration, no credentials.
- `help` / `H`, `diag on`, `diag off`; optional `wifi ` prefix is retained.

`W` now opens the WiFi menu instead of entering setup immediately. WiFi option
`7` requires confirmation before forwarding `forget`; the full command retains
its previous direct behavior. The private `serial_menu.c` owns only menu tables,
command normalization and routing. The existing worker remains the sole USB
reader and executes all commands, including those forwarded by numbered options.
SSID/password/host and scan-selection fields bypass menu routing entirely.
An oversized command is discarded with a message and never dispatched.

The inspection menu forwards `screen show NAME`, `screen status` and `screen live`
to the application handler. Full `screen show NAME` commands also select that
submenu, so `0`/`M` can exit consistently. The UI handler only queues requests;
all LVGL calls remain on the GUI owner. Dedicated touch testing, system diagnostics
and per-module log levels are marked pending and only print a notice.
Audio volume/play entries show the existing full
command syntax. GUI trace toggles work even if Hosted/WiFi initialization failed.
No new LVGL API calls, worker tasks or NVS schemas are introduced.

Moonraker accepts a DNS hostname or IPv4 address, up to 63 bytes, with port
1..65535. URLs, paths, userinfo and IPv6 literals are rejected. Only a successful
NVS commit publishes a changed endpoint/generation. No Moonraker command is
sent by this component; the client observes the snapshot.

Association runs in the network worker and can take three 20-second attempts.
Failed attempts preserve connection intent; retries continue after 15 seconds
even while the console waits for input. All connection APIs remain owned by
that worker; event callbacks only publish state and signal events. GUI operation
continues during scan, association, DNS/ping diagnostics and console input.
`check` checks gateway ping and DNS/TCP to example.com:443, which is a general
network diagnostic and does not establish Moonraker connectivity or TLS.

`device_network_console_active()` signals execution of console commands and
interactive menus so other application services can suppress optional logs.
It does not globally intercept logs from ESP-IDF or Hosted.

Pure C endpoint regression: compile `tests/network_validation_regression.c`
with a native C compiler and execute it. It tests the actual header used by the
component. Transport, DHCP, AP loss/recovery, NVS power-cycle persistence and USB
prompt behavior still require hardware validation. No COM port is used by this
test.

Navigation regression: compile `tests/serial_menu_regression.c` together with
`serial_menu.c` using a native C compiler (`-Wall -Werror`) and run the executable.
It exercises transitions between every pair of sections, numeric routing, return
to main, command normalization, legacy-command passthrough in every section,
invalid numeric input and the confirmation boundary for forgetting WiFi. Use
`--menus` to print all menus/prompts for review. This test uses neither the USB
port nor network/NVS/LVGL; it does not validate live input or confirmation entry.

Local verification: TinyCC `-Wall -Werror -run
components/device_network/tests/network_validation_regression.c` passed. On
this Windows installation TinyCC needed execution outside the sandbox because
the sandbox process launch failed with a system DLL relocation error. This test
does not compile the ESP-IDF integration or exercise its runtime services.
# Ajustes tactiles asincronos

`device_network_scan_async`, `device_network_save_wifi_async`,
`device_network_save_endpoint_async` y `device_network_test_endpoint_async`
copian su entrada y regresan sin esperar WiFi, NVS ni HTTP. Se acepta una sola
solicitud pendiente; `ESP_ERR_INVALID_STATE` indica inicio no completado u otra
solicitud en curso. `device_network_settings_snapshot` publica estado, resultado,
revision y hasta 16 redes (SSID, RSSI y necesidad de clave). Nunca publica claves.

El worker existente es el unico propietario de las operaciones WiFi y atiende
la cola al sondear la consola, tambien durante una pregunta interactiva. Serial
y tactil comparten validacion y guardado; sus cambios se aplican secuencialmente.
Una operacion serial o una reconexion en curso puede demorar una solicitud de
la GUI, pero nunca bloquea LVGL. La cola y las copias temporales de credenciales
se limpian al consumirlas. No se crea otro worker WiFi antes de Hosted.

WiFi se persiste antes de sustituir la conexion activa. Si se guarda pero no
se obtiene IP, el estado lo informa y se mantiene el reintento automatico.
La prueba de Moonraker usa HTTP `/server/info`, sin redirecciones, timeout de
4 segundos por operacion HTTP y cuerpo acotado a 2047 bytes; exige HTTP 200 y
`result.moonraker_version` JSON. Probar no persiste. DNS puede tardar segun el
resolver de lwIP. Los errores visibles son textos fijos, sin cuerpos remotos.

El overlay `portal_network_settings` pertenece al task LVGL y usa solo esta API.
El integrador debe pasar un guard que cubra inspeccion, solicitudes de inspeccion
pendientes y suspension; cerrar al cambiar de vista. Cada callback y tick
verifica ese guard. El teclado oculta la clave incluso mientras se escribe.
Los SSID con bytes no ASCII se muestran sustituidos por `?`, pero seleccionar
la red conserva los bytes originales para conectar; editar el campo cambia a
SSID manual. Cerrar el overlay no cancela una operacion ya aceptada.

