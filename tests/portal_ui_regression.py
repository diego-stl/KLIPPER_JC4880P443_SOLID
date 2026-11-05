"""Exercise real LVGL widget callbacks against recorded mock backend calls."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "tests/portal_host/output"
EXE = OUT / "portal_host.exe"
INSPECTION_VIEWS = ("startup", "standby", "printing", "paused", "toolhead", "files",
                    "macros", "exclude", "settings", "error", "details")

# Name, view, real widget events, required backend fragment, forbidden fragment.
CASES = [
    ("pause", 2, ["PAUSAR"], "CALL gcode PAUSE", None),
    ("resume", 3, ["REANUDAR"], "CALL gcode RESUME", None),
    ("cancel_back", 2, ["CANCELAR", "VOLVER"], None, "CALL gcode"),
    ("cancel_confirm", 2, ["CANCELAR", "CONFIRMAR"], "CALL gcode CANCEL_PRINT", None),
    ("target_back", 4, ["210 °C", "BORRAR", "2", "2", "0", "VOLVER"], None, "CALL gcode"),
    ("target_apply", 4, ["210 °C", "BORRAR", "2", "2", "0", "APLICAR"], "CALL gcode M104 S220", None),
    ("bed_apply", 4, ["60 °C", "BORRAR", "7", "0", "APLICAR"], "CALL gcode M140 S70", None),
    ("target_limit", 4, ["210 °C", "BORRAR", "9", "9", "9", "APLICAR"], None, "CALL gcode"),
    ("material_select_only", 4, ["@dropdown:0:1"], None, "CALL gcode"),
    ("material_apply", 4, ["@dropdown:0:1", "APLICAR"], "CALL gcode M140 S65\nM104 S200", None),
    ("cool", 4, ["ENFRIAR"], "CALL gcode M104 S0\nM140 S0", None),
    ("file_select_only", 5, ["portal_turret.gcode"], "CALL fetch_metadata portal_turret.gcode", "CALL print"),
    ("file_no_confirmation", 5, ["IMPRIMIR", "@expectnot:CONFIRMAR", "@expectprints:1"], "CALL print aperture_companion_cube.gcode", None),
    ("file_direct_double_click", 5, ["@savebutton:IMPRIMIR", "IMPRIMIR", "@clicksaved", "@expectprints:1"], "EXPECT_PRINTS 1 PASS", None),
    ("macro_select_only", 6, ["CLEAN_NOZZLE"], None, "CALL macro"),
    ("macro_run", 6, ["CLEAN_NOZZLE", "EJECUTAR"], "CALL macro CLEAN_NOZZLE", None),
    ("object_select_only", 7, ["cube_2"], None, "CALL exclude"),
    ("object_exclude", 7, ["cube_2", "EXCLUIR SELECCION"], "CALL exclude cube_2", None),
    ("home", 4, ["HOME TODOS"], "CALL gcode G28", None),
    ("jog_x", 4, ["X +"], "G1 X10 F6000", None),
    ("retract", 3, ["RETRAER 10 mm"], "G1 E-10 F300", None),
    ("emergency_back", 1, ["PARADA", "VOLVER"], None, "CALL emergency"),
    ("emergency_confirm", 1, ["PARADA", "CONFIRMAR"], "CALL emergency", None),
    ("restart_direct_error", 9, ["REINICIAR KLIPPER", "@expectnot:CONFIRMAR"], "CALL gcode FIRMWARE_RESTART", None),
    ("shutdown_back", 9, ["APAGAR HOST", "VOLVER"], None, "CALL shutdown"),
    ("shutdown_confirm", 9, ["APAGAR HOST", "CONFIRMAR"], "CALL shutdown", None),
    ("brightness", 8, ["@slider:55"], "CALL settings brightness=55 sleep=10", None),
    ("timeout", 8, ["@dropdown:0:1"], "CALL settings brightness=80 sleep=5", None),
    ("print_state_changed", 5, ["@state:3", "IMPRIMIR", "@expectprints:0"], None, "CALL print"),
    ("cancel_state_changed", 2, ["CANCELAR", "@state:2", "CONFIRMAR"], None, "CALL gcode CANCEL_PRINT"),
    ("cancel_job_changed", 2, ["CANCELAR", "@filename:different_job.gcode", "CONFIRMAR"], None, "CALL gcode CANCEL_PRINT"),
    ("print_endpoint_changed", 5, ["@generation:2", "IMPRIMIR", "@expectprints:0"], None, "CALL print"),
    ("cancel_endpoint_changed", 2, ["CANCELAR", "@generation:2", "CONFIRMAR"], None, "CALL gcode CANCEL_PRINT"),
    ("target_endpoint_changed", 4, ["210 °C", "BORRAR", "2", "2", "0", "@generation:2", "APLICAR"], None, "CALL gcode"),
    ("boot_error_recovery", 0, ["@booterror", "@tick", "@expect:ERROR", "@expect:MCU unavailable: cannot connect to printer MCU", "@capture:boot_error", "@recover", "@tick", "@expect:Cargando...", "@capture:boot_recovery"], "EXPECT_LABEL Cargando... PASS", "CALL gcode"),
    ("unknown_persists_through_pause", 2, ["@resultunknown", "@tick", "@expect:RESULTADO INCIERTO", "@state:4", "@tick", "@expect:EN PAUSA", "@expect:RESULTADO INCIERTO", "@capture:unknown_paused", "CERRAR", "@expectnot:RESULTADO INCIERTO"], "EXPECT_ABSENT RESULTADO INCIERTO PASS", "CALL gcode"),
    ("audio_default_50", 8, ["@expectaudio:1:50:0", "@expect:Listo: voces de GLaDOS"], "EXPECT_AUDIO enabled=1 volume=50 queued=0 PASS", "CALL gcode"),
    ("audio_explicit_test", 8, ["PROBAR AUDIO", "@expectaudio:1:50:1"], "CALL audio_play wakeup ACCEPTED queued=1 volume=50", "CALL gcode"),
    ("audio_volume_75", 8, ["@slider:1:75", "@expectaudio:1:75:0"], "CALL audio_volume 75", "CALL gcode"),
    ("audio_volume_zero", 8, ["@slider:1:0", "@tick", "@show:8", "@expectaudio:1:0:0", "@expect:Volumen a cero", "@expectbuttondisabled:PROBAR AUDIO"], "CALL audio_volume 0", "CALL gcode"),
    ("audio_volume_100", 8, ["@slider:1:100", "@expectaudio:1:100:0"], "CALL audio_volume 100", "CALL gcode"),
    ("audio_mute_flush_reject", 8, ["PROBAR AUDIO", "@switch:0:0", "@tick", "@show:8", "@expectaudio:0:50:0", "@expect:Silenciado; avisos vaciados", "@expectbuttondisabled:PROBAR AUDIO", "@audiotest_direct"], "CALL audio_test REJECTED", "CALL gcode"),
    ("audio_enable_preserves_volume", 8, ["@switch:0:0", "@switch:0:1", "@tick", "@show:8", "@expectaudio:1:50:0", "PROBAR AUDIO"], "CALL audio_play wakeup ACCEPTED queued=1 volume=50", "CALL gcode"),
    ("audio_not_ready", 8, ["@audioready:0", "@tick", "@show:8", "@expect:Audio no disponible; revisa consola", "@expectbuttondisabled:PROBAR AUDIO", "@audiotest_direct"], "CALL audio_test REJECTED", "CALL gcode"),
    ("voices_all_transitions", 0, ["@eventsstart", "@voice_ready:2", "@voice_ready:2", "@voice_ready:3", "@voice_ready:4", "@voice_ready:3", "@voice_ready:6", "@voice_ready:2", "@voice_ready:7", "@voice_ready:5", "@voice_ready:2", "@expectvoices:1:2:1:1:1:1:2", "@voice_status"], "EXPECT_VOICES 1:2:1:1:1:1:2 PASS", "CALL gcode"),
    ("voices_initial_printing_baseline", 0, ["@eventsstart", "@voice_ready:3", "@voice_ready:3", "@expectvoices:1:0:0:0:0:0:0"], "EXPECT_VOICES 1:0:0:0:0:0:0 PASS", "CALL gcode"),
    ("voices_initial_error_baseline", 0, ["@eventsstart", "@voice_error", "@voice_error", "@expectvoices:1:0:0:0:0:0:0"], "EXPECT_VOICES 1:0:0:0:0:0:0 PASS", "CALL gcode"),
    ("voices_reconnect_silent", 0, ["@eventsstart", "@voice_ready:2", "@voice_ready:3", "@voice_offline", "@voice_ready:4", "@voice_ready:4", "@expectvoices:1:1:0:0:0:0:0", "@voice_ready:3", "@expectvoices:1:2:0:0:0:0:0", "@voice_status"], "EXPECT_VOICES 1:2:0:0:0:0:0 PASS", "CALL gcode"),
    ("voices_endpoint_silent", 0, ["@eventsstart", "@voice_ready:2", "@voice_endpoint:2", "@voice_ready:3", "@expectvoices:1:1:0:0:0:0:0", "@voice_endpoint:3", "@expectvoices:1:1:0:0:0:0:0", "@voice_status"], "EXPECT_VOICES 1:1:0:0:0:0:0 PASS", "CALL gcode"),
    ("voices_muted_baseline_no_replay", 0, ["@eventsstart", "@voice_enabled:0", "@voice_partial", "@voice_ready:2", "@voice_ready:3", "@voice_enabled:1", "@voice_ready:3", "@expectvoices:0:0:0:0:0:0:0", "@voice_ready:4", "@expectvoices:0:0:1:0:0:0:0", "@voice_status"], "EXPECT_VOICES 0:0:1:0:0:0:0 PASS", "CALL gcode"),
    ("voices_volume_zero_boot_no_replay", 0, ["@eventsstart", "@voice_volume:0", "@voice_ready:2", "@voice_ready:3", "@voice_volume:50", "@voice_ready:3", "@expectvoices:0:0:0:0:0:0:0", "@voice_ready:4", "@expectvoices:0:0:1:0:0:0:0"], "EXPECT_VOICES 0:0:1:0:0:0:0 PASS", "CALL gcode"),
    ("voices_audio_ready_gating", 0, ["@eventsstart", "@audioready:0", "@voice_ready:2", "@voice_ready:3", "@expectvoices:0:0:0:0:0:0:0", "@audioready:1", "@voice_ready:3", "@expectvoices:1:0:0:0:0:0:0", "@voice_ready:4", "@expectvoices:1:0:1:0:0:0:0"], "EXPECT_VOICES 1:0:1:0:0:0:0 PASS", "CALL gcode"),
    ("voices_partial_snapshot_gate", 0, ["@eventsstart", "@voice_partial", "@voice_partial", "@expectvoices:1:0:0:0:0:0:0", "@voice_ready:3", "@expectvoices:1:0:0:0:0:0:0", "@voice_ready:4", "@expectvoices:1:0:1:0:0:0:0"], "EXPECT_VOICES 1:0:1:0:0:0:0 PASS", "CALL gcode"),
    ("inspection_all_views", 1,
     ["@savevoices"] + [step for i, name in enumerate(INSPECTION_VIEWS)
        for step in (f"@screen:screen show {name}", "@tick", f"@expectinspection:1:{i}:0")]
     + ["@expectvoicesunchanged", "@capture:inspection_details", "@screen:screen live", "@tick", "@expectinspection:0:1:0"],
     "EXPECT_INSPECTION_VOICES_UNCHANGED PASS", "CALL gcode"),
    ("inspection_holds_and_restores_latest", 1,
     ["@screen:screen show printing", "@expectinspection:0:1:1", "@tick", "@state:4", "@tick",
      "@expectinspection:1:2:0", "@expect:IMPRIMIENDO", "@screen:screen live", "@tick",
      "@expectinspection:0:3:0", "@expect:EN PAUSA"], "SCREEN_LIVE view=paused", "CALL gcode"),
    ("inspection_readonly_toolhead", 4,
     ["@screen:screen show toolhead", "HOME TODOS", "@tick", "HOME TODOS", "X +", "210 °C",
      "@expectnot:OBJETIVO - BOQUILLA", "@dropdown:0:1", "APLICAR", "@expectinspection:1:4:0"],
     "EXPECT_INSPECTION active=1 view=4 pending=0 PASS", "CALL gcode"),
    ("inspection_readonly_settings_and_sleep", 8,
     ["@savevoices", "@screen:screen show settings", "@tick", "@slider:55", "@slider:1:75",
      "@switch:0:0", "@dropdown:0:1", "PROBAR AUDIO", "@tick", "@expectaudio:1:50:0", "@expectdropdown:0:2",
      "@advance:650000", "@expectinspection:1:8:0", "@expectvoicesunchanged", "@capture:inspection_settings"],
     "EXPECT_INSPECTION_VOICES_UNCHANGED PASS", "CALL backlight off"),
    ("inspection_cancels_action_modal", 2,
     ["CANCELAR", "@screen:screen show settings", "@tick", "@expectinspection:1:8:0",
      "@expectnot:CANCELAR IMPRESION", "@screen:screen live", "@tick", "@expectnot:CANCELAR IMPRESION"],
     "SCREEN_LIVE view=printing", "CALL gcode"),
    ("inspection_preserves_unknown_notice", 2,
     ["@resultunknown", "@tick", "@screen:screen show settings", "@tick", "@expectinspection:0:2:0",
      "@expect:RESULTADO INCIERTO", "CERRAR", "@screen:screen show settings", "@tick", "@expectinspection:1:8:0"],
     "SCREEN_REFUSED", "CALL gcode"),
    ("inspection_unknown_returns_live", 2,
     ["@screen:screen show settings", "@tick", "@resultunknown", "@tick",
      "@expectinspection:0:2:0", "@expect:RESULTADO INCIERTO", "CERRAR"],
     "SCREEN_LIVE view=printing reason=command_result", "CALL gcode"),
    ("inspection_invalid_and_latest_request", 1,
     ["@screen:screen show invalid", "@expectinspection:0:1:0", "@screen:screen show printing",
      "@screen:screen show paused", "@expectinspection:0:1:1", "@tick", "@expectinspection:1:3:0",
      "@screen:screen status", "@screen:screen live", "@tick", "@expectinspection:0:1:0"],
     "SCREEN_ERROR vista invalida", "CALL gcode"),
    ("inspection_restores_error_generation", 1,
     ["@screen:screen show printing", "@tick", "@generation:2", "@state:7", "@tick",
      "@expectinspection:1:2:0", "@screen:screen live", "@tick", "@expectinspection:0:9:0", "@expect:ERROR"],
     "SCREEN_LIVE view=error", "CALL gcode"),
    ("inspection_offline_returns_startup", 0,
     ["@screen:screen show error", "@tick", "@expectinspection:1:9:0", "@screen:screen live",
      "@tick", "@expectinspection:0:0:0", "@expect:Cargando..."],
     "SCREEN_LIVE view=startup", "CALL gcode"),
    ("character_standby", 1, ["@expectcharacter:1"], "EXPECT_CHARACTER head=1 expected=1 PASS", None),
    ("character_complete", 1, ["@lastjob", "@tick", "@expectcharacter:1", "@capture:qa_character_complete"], "EXPECT_CHARACTER head=1 expected=1 PASS", None),
    ("character_cancelled", 1, ["@state:6", "@tick", "@expectcharacter:1", "@capture:qa_character_cancelled"], "EXPECT_CHARACTER head=1 expected=1 PASS", None),
    ("character_error", 9, ["@expectcharacter:1", "@capture:qa_character_error"], "EXPECT_CHARACTER head=1 expected=1 PASS", None),
    ("artwork_standby", 1, ["@expectgridicons", "@expecticon:nozzle:32", "@expecticon:bed:32", "@expecticon:wifi:32", "@capture:qa_artwork_standby"], "EXPECT_GRID_ARTWORK six visible semantic icons PASS", None),
    ("artwork_printing", 2, ["@expectgridicons", "@expecticon:nozzle:32", "@expecticon:bed:32", "@capture:qa_artwork_printing"], "EXPECT_GRID_ARTWORK six visible semantic icons PASS", None),
    ("artwork_paused", 3, ["@expectgridicons", "@capture:qa_artwork_paused"], "EXPECT_GRID_ARTWORK six visible semantic icons PASS", None),
    ("artwork_toolhead", 4, ["@expectbuttonicon:up:32:Y +", "@expectbuttonicon:right:32:X +", "@expectbuttonicon:home:32:XY", "@expectbuttonicon:plus:32:Z +", "@expectbuttonicon:minus:32:Z -", "@expectbuttonicon:cool:32:ENFRIAR", "@expectbuttonicon:motors_off:32:MOTORES OFF", "@expecticon:back:32", "@capture:qa_artwork_toolhead"], "EXPECT_BUTTON_ARTWORK MOTORES OFF icon=motors_off size=32 text_clear=1 PASS", "CALL gcode"),
    ("artwork_settings_credit_audio", 8, ["@expecticon:audio:32", "@expectbuttonicon:wifi:32:WI-FI Y MOONRAKER", "@expect:Uicons by Flaticon  |  www.flaticon.com/uicons", "@switch:0:0", "@tick", "@expecticon:mute:32", "@capture:qa_artwork_settings_credit"], "ARTWORK mute", "CALL gcode"),
    ("artwork_startup_logos", 0, ["@expectlogos", "@expecticon:wifi:32", "@expectbuttonicon:settings:32:AJUSTES", "@expect:A P E R T U R E", "@expect:Klipper", "@capture:qa_artwork_startup_logos"], "EXPECT_APPROVED_LOGOS visible_without_recolor PASS", None),
    ("artwork_error", 9, ["@expectbuttonicon:refresh:64:REINICIAR KLIPPER", "@expectbuttonicon:power:64:APAGAR HOST", "@expectbuttonicon:settings:32:AJUSTES", "@capture:qa_artwork_error"], "EXPECT_BUTTON_ARTWORK APAGAR HOST icon=power size=64 text_clear=1 PASS", "CALL gcode"),
    ("artwork_macros", 6, ["@expectbuttonicon:play:32:EJECUTAR", "@capture:qa_artwork_macros"], "EXPECT_BUTTON_ARTWORK EJECUTAR icon=play size=32 text_clear=1 PASS", "CALL macro"),
    ("grid_standby", 1, ["@expectgrid"], "EXPECT_GRID six equal buttons=6", None),
    ("grid_printing", 2, ["@expectgrid"], "EXPECT_GRID six equal buttons=6", None),
    ("grid_paused", 3, ["@expectgrid"], "EXPECT_GRID six equal buttons=6", None),
    ('thumbnail_100_files', 5, ['@thumbnail:100', '@tick', '@expectthumbcount:1', '@expectimage:100:300', '@capture:qa_thumbnail_100_files'], 'EXPECT_IMAGE source=100 visual=300', None),
    ('thumbnail_300_files', 5, ['@thumbnail:300', '@tick', '@expectthumbcount:1', '@expectimage:300:300', '@capture:qa_thumbnail_300_files'], 'EXPECT_IMAGE source=300 visual=300', None),
    ("thumbnail_100_printing", 2, ["@thumbnail:100", "@tick", "@expectimage:100:200", "@capture:qa_thumbnail_100_printing"], "EXPECT_IMAGE source=100 visual=200", None),
    ("thumbnail_300_printing", 2, ["@thumbnail:300", "@tick", "@expectimage:300:200", "@capture:qa_thumbnail_300_printing"], "EXPECT_IMAGE source=300 visual=200", None),
    ("thumbnail_100_details", 10, ["@thumbnail:100", "@tick", "@expectimage:100:300", "@capture:qa_thumbnail_100_details"], "EXPECT_IMAGE source=100 visual=300", None),
    ("thumbnail_300_details", 10, ["@thumbnail:300", "@tick", "@expectimage:300:300", "@capture:qa_thumbnail_300_details"], "EXPECT_IMAGE source=300 visual=300", None),
    ('file_list_names_only', 5, ['@thumbnail:300', '@tick', '@expectthumbcount:1', '@expectimage:300:300', '@expect:portal_turret.gcode', '@expect:calibration_cube.gcode', '@expectnot:ESTIMADO', '@expectnot:FILAMENTO', '@expectnot:42.6 g', '@expectfileslist', '@expectprintfooter', '@capture:qa_file_list_names'], 'EXPECT_PRINT_FOOTER text_only_48px PASS', None),
    ('thumbnail_no_data', 5, ['@thumbnail:0', '@tick', '@expect:NO DATA', '@expectthumbcount:0', '@capture:qa_no_data'], 'EXPECT_LABEL NO DATA PASS', None),
    ('file_long_name', 5, ['@longname', '@thumbnail:300', '@tick', '@show:5', '@expectimage:300:300', '@expectthumbcount:1', '@capture:qa_long_filename'], 'EXPECT_IMAGE source=300 visual=300', None),
    ('filament_one', 5, ['@thumbnail:300', '@tools:1', '@tick', '@expectswatches:1', '@expectcolor:#FFAA22:1', '@expectnot:T0', '@expectnot:42.6 g', '@capture:qa_filament_one'], 'EXPECT_SWATCHES 1 expected=1 PASS', None),
    ('filament_two', 5, ['@thumbnail:300', '@tools:2', '@tick', '@expectswatches:2', '@expectcolor:#FFAA22:1', '@expectcolor:#EEEEEE:1', '@expectnot:T0', '@expectnot:T1', '@expectnot:21.3 g', '@capture:qa_filament_two'], 'EXPECT_SWATCHES 2 expected=2 PASS', None),
    ('filament_many', 5, ['@thumbnail:300', '@tools:10', '@tick', '@expectswatches:10', '@expectnot:T0', '@expectnot:T1', '@expectnot:T2', '@expectnot:T3', '@expectnot:T4', '@expectnot:T5', '@expectnot:T6', '@expectnot:T7', '@expectnot:T8', '@expectnot:T9', '@expectnot:4.3 g', '@capture:qa_filament_many', '@scrollswatch:9', '@capture:qa_filament_many_last'], 'EXPECT_SWATCHES 10 expected=10 PASS', None),
    ('filament_orca_zero_slots', 5, ['@thumbnail:300', '@tools:orca', '@tick', '@expectswatches:4', '@expectcolor:#FFAA22:2', '@expectcolor:#EEEEEE:2', '@expectnot:T0', '@expectnot:T1', '@expectnot:T2', '@expectnot:T3', '@expectnot:T4', '@expectnot:T5', '@expectnot:T6', '@expectnot:T7', '@expectnot:5.6 g', '@expectnot:1.5 g', '@expectnot:0.5 g', '@capture:qa_filament_orca', '@show:10', '@expect:4 filamentos'], 'EXPECT_LABEL 4 filamentos PASS', None),
    ('filament_duplicate_colors', 5, ['@thumbnail:300', '@tools:same', '@tick', '@expectswatches:2', '@expectcolor:#FFAA22:2', '@expectnot:T0', '@expectnot:T1', '@capture:qa_filament_same_colors'], 'EXPECT_COLOR #FFAA22 count=2 PASS', None),
    ('filament_missing', 5, ['@thumbnail:300', '@tools:0', '@tick', '@expectswatches:0', '@expectnot:T0', '@capture:qa_filament_missing'], 'EXPECT_SWATCHES 0 expected=0 PASS', None),
    ("last_job_reset_preserved", 1, ["@thumbnail:300", "@lastjob", "@tick", "@show:1", "ULTIMO TRABAJO", "@expect:COMPLETADO", "@expectimage:300:300", "LIMPIAR ESTADO Y VOLVER A ESPERA", "@tick", "@expectlastjob", "ULTIMO TRABAJO", "@expect:COMPLETADO", "@expectimage:300:300", "@advance:3000", "@capture:qa_last_job_after_reset"], "CALL reset_job 42", "CALL print"),
    ("last_job_current_file_cleared", 1, ["@thumbnail:300", "@lastjob", "@clearcurrent", "@tick", "ULTIMO TRABAJO", "@expect:COMPLETADO", "@expectimage:300:300", "@expectlastjob"], "EXPECT_LAST_JOB preserved=1", "CALL reset_job"),
    ("macro_hidden_prefix", 6, ["@hiddenmacros", "@tick", "@expectnotbutton:_INTERNAL_HELPER", "@expectnotbutton:__PRIVATE_START", "@expect:CLEAN_NOZZLE", "@show:6", "@expectnot:_INTERNAL_HELPER", "@expectnot:__PRIVATE_START", "@expectbuttondisabled:EJECUTAR", "@forceclick:EJECUTAR", "@capture:qa_macros_filtered"], "EXPECT_ABSENT_BUTTON _INTERNAL_HELPER PASS", "CALL macro"),
    ("network_offline_access", 8, ["@network:offline", "@tick", "WI-FI Y MOONRAKER", "@expectnetwork:1", "@expect:RED Y MOONRAKER", "@capture:qa_network_offline", "Cerrar", "@expectnetwork:0"], "EXPECT_NETWORK open=1 PASS", "CALL gcode"),
    ("network_scan_async", 8, ["WI-FI Y MOONRAKER", "Escanear", "@expect:Solicitud pendiente...", "@netcomplete:ok", "@tick", "@dropdown:0:1", "@expect:Red seleccionada: introduce la clave", "@capture:qa_network_scan"], "CALL network_scan", "CALL gcode"),
    ("network_busy_mailbox", 8, ["WI-FI Y MOONRAKER", "Escanear", "Escanear", "@expect:Red iniciando u ocupada; espera y vuelve a intentar", "@netcomplete:ok", "@tick", "@expect:Operacion completada"], "CALL network_scan", "CALL gcode"),
    ("network_wifi_save", 8, ["WI-FI Y MOONRAKER", "Escanear", "@netcomplete:ok", "@tick", "@dropdown:0:1", "@textarea:1:fixture-secret", "Guardar y conectar WiFi", "@expectpasswordempty", "@netcomplete:ok", "@tick"], "CALL network_wifi ssid=Aperture Lab secured=1 password_redacted=1", "fixture-secret"),
    ("network_test_error_reconnect", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@textarea:0:192.168.1.121", "@textarea:1:7125", "Probar conexion", "@expect:Solicitud pendiente...", "@netcomplete:error", "@tick", "@expect:Error de conexion; vuelve a intentar", "@capture:qa_network_error", "Probar conexion", "@netcomplete:ok", "@network:reconnect", "@tick", "@expect:Operacion completada", "@capture:qa_network_reconnect"], "CALL network_test host=192.168.1.121 port=7125", "CALL gcode"),
    ("network_endpoint_save", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@textarea:0:192.168.1.121", "@textarea:1:7125", "Guardar Moonraker", "@netcomplete:ok", "@tick", "@expectnetwork:0"], "CALL network_endpoint host=192.168.1.121 port=7125", "CALL gcode"),
    ("network_numeric_ip_keyboard", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@focus:0", "@expectkeyboard:1", "@expectnumericdots", "@textarea:0:192.168.1.121", "@capture:qa_network_numeric_ip", "@keyboarddone", "@focus:1", "@expectnumericdots", "@keyboarddone", "Cerrar"], "EXPECT_NUMERIC_DOTS PASS", "CALL network_"),
    ("network_validation", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@textarea:0:http://bad/path", "Probar conexion", "@expect:Revisa SSID, clave (8-63 bytes o 64 hex), IPv4 y puerto (1-65535)"], "EXPECT_LABEL Revisa SSID", "CALL network_test"),
    ("network_keyboard", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@expectkeyboard:1", "@textarea:0:Manual Network", "@capture:qa_network_keyboard", "@keyboarddone", "@expectkeyboard:0", "@expectnetwork:1", "@capture:qa_network_manual"], "EXPECT_NETWORK open=1 PASS", "CALL network_wifi"),
    ("inspection_network_pending_hidden", 8, ["WI-FI Y MOONRAKER", "@expectnetwork:1", "@screen:screen show startup", "@tick", "@expectnetwork:0", "@expectinspection:1:0:0", "@capture:qa_network_guard_hidden"], "EXPECT_NETWORK open=0 PASS", "CALL network_"),
    ("inspection_network_pending_keyboard", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@expectkeyboard:1", "@screen:screen show startup", "@tick", "@expectnetwork:0", "@expectinspection:1:0:0", "@capture:qa_network_guard_keyboard"], "EXPECT_NETWORK open=0 PASS", "CALL network_"),
    ("inspection_network_close_pending_keyboard", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@expectkeyboard:1", "@screen:screen show startup", "Cerrar", "@expectnetwork:0", "@tick", "@expectinspection:1:0:0"], "EXPECT_NETWORK open=0 PASS", "CALL network_"),
    ("network_endpoint_change_keyboard", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@expectkeyboard:1", "@generation:2", "@tick", "@expectnetwork:0"], "EXPECT_NETWORK open=0 PASS", "CALL network_"),
    ("network_unknown_result_keyboard", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@expectkeyboard:1", "@resultunknown", "@tick", "@expectnetwork:0", "@expect:RESULTADO INCIERTO", "@capture:qa_network_unknown", "CERRAR", "@expectnot:RESULTADO INCIERTO"], "EXPECT_LABEL RESULTADO INCIERTO PASS", "CALL network_"),
    ("network_close_visible_keyboard", 8, ["WI-FI Y MOONRAKER", "@focus:1", "@expectkeyboard:1", "Cerrar", "@expectnetwork:0", "@tick"], "EXPECT_NETWORK open=0 PASS", "CALL network_"),
    ("network_startup_access", 0, ["AJUSTES", "WI-FI Y MOONRAKER", "@tick", "@expectnetwork:1", "@expect:RED Y MOONRAKER", "@capture:qa_network_from_startup", "Cerrar", "@expectnetwork:0"], "EXPECT_NETWORK open=1 PASS", "CALL network_"),
    ("network_from_error", 9, ["AJUSTES", "WI-FI Y MOONRAKER", "@expectnetwork:1", "@tick", "@expectnetwork:1"], "EXPECT_NETWORK open=1 PASS", "CALL gcode"),
    ("inspection_blocks_network", 8, ["@screen:screen show settings", "@tick", "@forceclick:WI-FI Y MOONRAKER", "@expectnetwork:0"], "EXPECT_NETWORK open=0 PASS", "CALL network_"),
    ("inspection_resume_controls", 2,
     ["@screen:screen show paused", "@tick", "@screen:screen live", "@tick", "PAUSAR"],
     "CALL gcode PAUSE", None),
    ("print_queue_navigates_without_fake_state", 5,
     ["IMPRIMIR", "@expectview:1", "@expectstate:2", "@expectnot:IMPRIMIENDO", "@capture:qa_print_queued",
      "@printresult:0", "@tick", "@expectview:1", "@expectstate:2", "@expectprints:1"],
     "EXPECT_PRINTS 1 PASS", "CALL gcode"),
    ("print_ack_waits_for_websocket", 5,
     ["IMPRIMIR", "@printresult:1", "@tick", "@expectview:1", "@expectstate:2",
      "@state:3", "@tick", "@expectview:2", "@expect:IMPRIMIENDO", "@expectprints:1"],
     "EXPECT_VIEW 2 expected=2 PASS", None),
    ("print_queue_rejected_keeps_files", 5,
     ["@printreject:1", "IMPRIMIR", "@expectview:5", "@expectprints:1"], "REJECT print queue", "CALL print"),
    ("print_async_rejected_visible", 5,
     ["IMPRIMIR", "@printresult:2", "@tick", "@expect:Impresion rechazada por Moonraker", "@expectprints:1"],
     "RESULT print status=2 popped", None),
    ("print_unknown_visible_no_resubmit", 5,
     ["@savebutton:IMPRIMIR", "IMPRIMIR", "@printresult:3", "@tick", "@expect:RESULTADO INCIERTO",
      "@clicksaved", "@expectprints:1", "@capture:qa_print_unknown"], "EXPECT_PRINTS 1 PASS", None),
    ("print_websocket_paused_transition", 5,
     ["IMPRIMIR", "@state:3", "@tick", "@expectview:2", "@state:4", "@tick", "@expectview:3"],
     "EXPECT_VIEW 3 expected=3 PASS", None),
    ("print_pending_blocks_move_route", 5,
     ["IMPRIMIR", "@forceclick:MOVER", "@expectnot:MOVIMIENTO", "@expectprints:1"],
     "EXPECT_ABSENT MOVIMIENTO PASS", "CALL gcode"),
    ("move_external_print_blocks_jog", 4,
     ["@savebutton:X +", "@state:3", "@clicksaved", "@tick", "@expectview:2", "@clicksaved"],
     "EXPECT_VIEW 2 expected=2 PASS", "CALL gcode"),
    ("tuning_action_printing", 2,
     ["@expectnotbutton:TOOLHEAD", "@expectbuttonicon:settings:64:AJUSTES IMPRESION", "AJUSTES IMPRESION",
      "@expect:AJUSTES DE IMPRESION", "@expecttuningvalue:0:100 %", "@expecttuningvalue:1:100 %", "@expecttuningvalue:2:50 %",
      "@capture:qa_tuning_printing", "CERRAR", "@expectnot:AJUSTES DE IMPRESION", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_action_paused", 3,
     ["@expectnotbutton:TOOLHEAD", "@expectbuttonicon:settings:64:AJUSTES IMPRESION", "AJUSTES IMPRESION",
      "@capture:qa_tuning_paused", "CERRAR", "@expectview:3", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_drafts_close_without_send", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@tuningclick:1:minus", "@tuningclick:2:plus",
      "@expecttuningvalue:0:105 %", "@expecttuningvalue:1:95 %", "@expecttuningvalue:2:55 %", "CERRAR",
      "AJUSTES IMPRESION", "@expecttuningvalue:0:100 %", "@expecttuningvalue:1:100 %", "@expecttuningvalue:2:50 %",
      "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_speed_apply_once", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@tuningclick:0:apply", "@tuningforce:0:apply", "@expecttuningcalls:1:0:0"],
     "CALL tuning control=0 percent=105 generation=1 job=42", "CALL gcode"),
    ("tuning_flow_apply", 3,
     ["AJUSTES IMPRESION", "@tuningclick:1:minus", "@tuningclick:1:apply", "@expecttuningcalls:0:1:0"],
     "CALL tuning control=1 percent=95 generation=1 job=42", "CALL gcode"),
    ("tuning_fan_apply", 2,
     ["AJUSTES IMPRESION", "@tuningclick:2:plus", "@tuningclick:2:apply", "@expecttuningcalls:0:0:1"],
     "CALL tuning control=2 percent=55 generation=1 job=42", "CALL gcode"),
    ("tuning_fan_missing", 2,
     ["@tuningvalue:2:0:0", "@tick", "AJUSTES IMPRESION", "@expecttuningvalue:2:-- %",
      "@expecttuningdisabled:2:plus", "@expecttuningdisabled:2:minus", "@expecttuningdisabled:2:apply",
      "@tuningforce:2:apply", "@expecttuningcalls:0:0:0", "@capture:qa_tuning_fan_missing"], None, "CALL tuning"),
    ("tuning_fan_out_of_physical_range", 2,
     ["@tuningvalue:2:1:999", "@tick", "AJUSTES IMPRESION", "@expecttuningdisabled:2:apply",
      "@tuningforce:2:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_live_actual_preserves_draft", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@tuningvalue:0:1:90", "@tick",
      "@expecttuningvalue:0:90 %", "@expecttuningvalue:0:105 %", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_validity_change_closes_modal", 2,
     ["AJUSTES IMPRESION", "@tuningvalue:2:0:0", "@tick", "@expectnot:AJUSTES DE IMPRESION", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_pause_transition_closes_modal", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@state:4", "@tick", "@expectnot:AJUSTES DE IMPRESION",
      "@expectview:3", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_endpoint_changes_before_apply", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@generation:2", "@tuningforce:0:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_job_changes_before_apply", 2,
     ["AJUSTES IMPRESION", "@tuningclick:1:plus", "@jobid:43", "@tuningforce:1:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_finished_before_apply", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@state:5", "@tuningforce:0:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_offline_before_apply", 2,
     ["AJUSTES IMPRESION", "@tuningclick:2:plus", "@network:offline", "@tuningforce:2:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("inspection_tuning_blocks_open", 2,
     ["@screen:screen show paused", "@tick", "@forceclick:AJUSTES IMPRESION", "@expectnot:AJUSTES DE IMPRESION", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("inspection_tuning_closes_modal", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@screen:screen show settings", "@tick",
      "@expectnot:AJUSTES DE IMPRESION", "@expectinspection:1:8:0", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_unknown_shields_controls", 2,
     ["AJUSTES IMPRESION", "@tuningclick:0:plus", "@resultunknown", "@tick", "@expect:RESULTADO INCIERTO",
      "@expectnot:AJUSTES DE IMPRESION", "@forceclick:AJUSTES IMPRESION", "@expectnot:AJUSTES DE IMPRESION", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
]

# Repeated real button events exercise the boundaries without test-only UI hooks.
for control, minimum, maximum, initial in ((0, 10, 200, 100), (1, 50, 150, 100), (2, 0, 100, 50)):
    for direction, boundary in (("minus", minimum), ("plus", maximum)):
        CASES.append((f"tuning_limit_{control}_{direction}", 2,
                      ["AJUSTES IMPRESION"] + [f"@tuningforce:{control}:{direction}"] * 45
                      + [f"@expecttuningvalue:{control}:{boundary} %", f"@tuningclick:{control}:apply",
                         "@expecttuningcalls:" + ":".join("1" if i == control else "0" for i in range(3))],
                      f"CALL tuning control={control} percent={boundary} generation=1 job=42", "CALL gcode"))
for control in range(3):
    for malformed in ("nan", "inf", "-1"):
        CASES.append((f"tuning_invalid_{control}_{malformed}", 2,
                      [f"@tuningvalue:{control}:1:{malformed}", "@tick", "AJUSTES IMPRESION",
                       f"@expecttuningdisabled:{control}:apply", f"@tuningforce:{control}:apply", "@expecttuningcalls:0:0:0"],
                      None, "CALL tuning"))


def main():
    fingerprints = json.loads((OUT / "build_sources.json").read_text())
    if any(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest for name, digest in fingerprints.items()):
        raise SystemExit("Host sources changed; run tools/render_portal_firmware.py first")
    logs = OUT / "actions"
    logs.mkdir(parents=True, exist_ok=True)
    results = []
    for name, view, actions, required, forbidden in CASES:
        encoded = [action if action.startswith("@") else "hex:" + action.encode("utf-8").hex() for action in actions]
        run = subprocess.run([str(EXE), str(OUT), str(view)] + encoded, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", timeout=30)
        (logs / (name + ".log")).write_text(run.stdout + run.stderr, encoding="utf-8")
        activity = run.stdout.split("ACTION_BEGIN\n", 1)[-1]
        success = run.returncode == 0 and (required is None or required in activity) and (forbidden is None or forbidden not in activity)
        if name.startswith("voices_") and re.search(r"^CALL (?:gcode|print|macro|exclude|emergency|shutdown|reset_job|network_[a-z]+)\b", activity, re.M):
            success = False
        if name.startswith("inspection_") and name != "inspection_resume_controls" and re.search(
                r"^CALL (?:gcode|print|macro|exclude|emergency|shutdown|reset_job|network_[a-z]+|settings|audio_enabled|audio_volume)\b", activity, re.M):
            success = False
        summary = re.search(r"SUMMARY .*clicks=(\d+)", activity)
        result = {"name": name, "passed": success, "events": int(summary[1]) if summary else 0,
                  "backend_calls": [line for line in activity.splitlines() if line.startswith("CALL ")],
                  "clipping": [line for line in activity.splitlines() if line.startswith("CLIP ")],
                  "mocked_audio": True, "real_audio_events": name.startswith("voices_")}
        results.append(result)
        print(f"{'PASS' if success else 'FAIL'} {name}: {result['events']} events")
    report = {"source_sha256": fingerprints, "regression_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "cases": results, "passed": sum(r["passed"] for r in results), "total": len(results),
              "events": sum(r["events"] for r in results), "hardware": False}
    (OUT / "action_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for raw in [raw for pattern in ("boot_*", "unknown_*", "inspection_*", "qa_*") for raw in OUT.glob(pattern + ".rgb565")]:
        Image.frombytes("RGB", (800, 480), raw.read_bytes(), "raw", "BGR;16").save(raw.with_suffix(".png"))
    print(f"{report['passed']}/{report['total']} scenarios; {report['events']} real LVGL events")
    raise SystemExit(0 if report["passed"] == report["total"] else 1)


if __name__ == "__main__":
    main()
