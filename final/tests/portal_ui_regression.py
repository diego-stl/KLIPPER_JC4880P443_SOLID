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
    ("printing_preview_details", 2,
     ["@pointer:1:180:130:20", "@pointer:0:180:130:20", "@expectview:10"], None, "CALL gcode"),
    ("paused_progress_details", 3,
     ["@pointer:1:180:310:20", "@pointer:0:180:310:20", "@expectview:10"], None, "CALL gcode"),
    ("cancel_back", 2, ["CANCELAR", "VOLVER"], None, "CALL gcode"),
    ("cancel_confirm", 2, ["CANCELAR", "CONFIRMAR"], "CALL gcode CANCEL_PRINT", None),
    ("target_back", 4, ["210 °C", "BORRAR", "2", "2", "0", "VOLVER"], None, "CALL gcode"),
    ("target_apply", 4, ["210 °C", "BORRAR", "2", "2", "0", "APLICAR", "@commandresult:0", "@tick",
     "@expectclip:temperature:0", "@commandresult:1", "@tick", "@tick", "@expectclip:temperature:1"], "CALL gcode M104 S220", None),
    ("bed_apply", 4, ["60 °C", "BORRAR", "7", "0", "APLICAR", "@commandresult:1", "@tick", "@expectclip:temperature:1"], "CALL gcode M140 S70", None),
    ("target_rejected_silent", 4, ["210 °C", "BORRAR", "2", "2", "0", "APLICAR",
     "@commandresult:2", "@tick", "@expectclip:temperature:0"], "CALL gcode M104 S220", None),
    ("target_old_endpoint_silent", 4, ["210 °C", "BORRAR", "2", "2", "0", "APLICAR",
     "@generation:2", "@commandresult:1", "@tick", "@expectclip:temperature:0"], "CALL gcode M104 S220", None),
    ("target_limit", 4, ["210 °C", "BORRAR", "9", "9", "9", "APLICAR"], None, "CALL gcode"),
    ("material_select_only", 4, ["@dropdown:0:1"], None, "CALL gcode"),
    ("material_apply", 4, ["@dropdown:0:1", "APLICAR", "@commandresult:1", "@tick", "@expectclip:temperature:1"], "CALL gcode M140 S65\nM104 S200", None),
    ("cool", 4, ["ENFRIAR", "@commandresult:1", "@tick", "@expectclip:temperature:1"], "CALL gcode M104 S0\nM140 S0", None),
    ("file_select_only", 5, ["portal_turret.gcode"], "CALL fetch_metadata portal_turret.gcode", "CALL print"),
    ("file_no_confirmation", 5, ["IMPRIMIR", "@expectnot:CONFIRMAR", "@expectprints:1"], "CALL print aperture_companion_cube.gcode", None),
    ("file_direct_double_click", 5, ["@savebutton:IMPRIMIR", "IMPRIMIR", "@clicksaved", "@expectprints:1"], "EXPECT_PRINTS 1 PASS", None),
    ("macro_select_only", 6, ["CLEAN_NOZZLE"], None, "CALL macro"),
    ("macro_run", 6, ["CLEAN_NOZZLE", "EJECUTAR"], "CALL macro CLEAN_NOZZLE", None),
    ("object_select_only", 7, ["cube_2"], None, "CALL exclude"),
    ("object_exclude", 7, ["cube_2", "EXCLUIR SELECCIÓN"], "CALL exclude cube_2", None),
    ("home", 4, ["HOME TODOS", "@commandresult:1", "@tick", "@expectclip:movement:1"], "CALL gcode G28", None),
    ("home_x", 4, ["X"], "CALL gcode G28 X\n", "CALL gcode G28 X Y"),
    ("home_y", 4, ["Y"], "CALL gcode G28 Y\n", "CALL gcode G28 X"),
    ("home_z", 4, ["Z"], "CALL gcode G28 Z\n", None),
    ("absolute_positions", 4, ["@expect:X/ 110", "@expect:Y/ 95", "@expect:Z/ 12.4",
      "@dropdown:1:4", "@expect:Z/ 12.4", "@homed:3", "@tick", "@expect:Z/ --"], None, "CALL gcode"),
    ("jog_x", 4, ["X +", "@commandresult:1", "@tick", "@expectclip:movement:1"], "G1 X10 F6000", None),
    ("extrude", 3, ["EXTRUIR 50 mm", "@commandresult:1", "@tick", "@expectclip:filament:1"], "M83\nG1 E50 F300\nRESTORE_GCODE_STATE NAME=JC_PANEL_E MOVE=0", None),
    ("retract", 3, ["RETRAER 50 mm", "@commandresult:1", "@tick", "@expectclip:filament:1"], "M83\nG1 E-50 F300\nRESTORE_GCODE_STATE NAME=JC_PANEL_E MOVE=0", None),
    ("emergency_back", 1, ["PARADA", "VOLVER", "@expectclip:system:0"], None, "CALL emergency"),
    ("emergency_confirm", 1, ["PARADA", "CONFIRMAR", "@expectclip:system:1"], "CALL emergency", None),
    ("restart_direct_error", 9, ["REINICIAR KLIPPER", "@expectnot:CONFIRMAR", "@expectclip:system:1"], "CALL gcode FIRMWARE_RESTART", None),
    ("shutdown_back", 9, ["APAGAR EQUIPO\nANFITRIÓN", "VOLVER", "@expectclip:system:0"], None, "CALL shutdown"),
    ("shutdown_confirm", 9, ["APAGAR EQUIPO\nANFITRIÓN", "@expectcore:warning", "@capture:qa_confirm_shutdown",
      "APAGAR", "@expectclip:system:1"], "CALL shutdown", None),
    ("brightness", 8, ["@slider:55", "@expectdisplaypending:1", "@expectwrites:0:0", "@displaycomplete:ok", "@expectwrites:1:0"], "CALL settings_persist brightness=55 sleep=10", None),
    ("timeout", 8, ["@dropdown:0:1", "@expectdisplaypending:1", "@displaycomplete:ok", "@expectwrites:1:0"], "CALL settings_persist brightness=80 sleep=5", None),
    ("print_state_changed", 5, ["@state:3", "IMPRIMIR", "@expectprints:0"], None, "CALL print"),
    ("cancel_state_changed", 2, ["CANCELAR", "@state:2", "CONFIRMAR"], None, "CALL gcode CANCEL_PRINT"),
    ("cancel_job_changed", 2, ["CANCELAR", "@filename:different_job.gcode", "CONFIRMAR"], None, "CALL gcode CANCEL_PRINT"),
    ("print_endpoint_changed", 5, ["@generation:2", "IMPRIMIR", "@expectprints:0"], None, "CALL print"),
    ("cancel_endpoint_changed", 2, ["CANCELAR", "@generation:2", "CONFIRMAR"], None, "CALL gcode CANCEL_PRINT"),
    ("target_endpoint_changed", 4, ["210 °C", "BORRAR", "2", "2", "0", "@generation:2", "APLICAR"], None, "CALL gcode"),
    ("boot_error_recovery", 0, ["@booterror", "@tick", "@expect:ERROR DE KLIPPER", "@expect:MCU unavailable: cannot connect to printer MCU", "@capture:boot_error", "@recover", "@tick", "@expect:Conectando Wi-Fi...", "@capture:boot_recovery"], "EXPECT_LABEL Conectando Wi-Fi... PASS", "CALL gcode"),
    ("unknown_persists_through_pause", 2, ["@resultunknown", "@tick", "@expect:RESULTADO INCIERTO", "@state:4", "@tick", "@expect:EN PAUSA", "@expect:RESULTADO INCIERTO", "@capture:unknown_paused", "CERRAR", "@expectnot:RESULTADO INCIERTO"], "EXPECT_ABSENT RESULTADO INCIERTO PASS", "CALL gcode"),
    ("audio_default_50", 8, ["@expectaudio:1:50:0", "@expect:Listo: sonidos de Portal"], "EXPECT_AUDIO enabled=1 volume=50 queued=0 PASS", "CALL gcode"),
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
      "@expectnot:CANCELAR IMPRESIÓN", "@screen:screen live", "@tick", "@expectnot:CANCELAR IMPRESIÓN"],
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
      "@expectinspection:1:2:0", "@screen:screen live", "@tick", "@expectinspection:0:9:0", "@expect:ERROR DE KLIPPER"],
     "SCREEN_LIVE view=error", "CALL gcode"),
    ("inspection_offline_returns_startup", 0,
     ["@screen:screen show error", "@tick", "@expectinspection:1:9:0", "@screen:screen live",
      "@tick", "@expectinspection:0:0:0", "@expect:Conectando Wi-Fi..."],
     "SCREEN_LIVE view=startup", "CALL gcode"),
    ("character_standby", 1, ["@expectcore:base"], "EXPECT_CORE base aligned and visible PASS", None),
    ("character_complete", 1, ["@lastjob", "@tick", "@expectcore:complete", "@capture:qa_character_complete"], "EXPECT_CORE complete aligned and visible PASS", None),
    ("character_cancelled", 1, ["@state:6", "@tick", "@expectcore:cancelled", "@capture:qa_character_cancelled"], "EXPECT_CORE cancelled aligned and visible PASS", None),
    ("character_error", 9, ["@expectcore:error", "@capture:qa_character_error"], "EXPECT_CORE error aligned and visible PASS", None),
    ("artwork_standby", 1, ["@expectgridicons", "@expecticon:nozzle:32", "@expecticon:bed:32", "@expectnoicon:wifi", "@capture:qa_artwork_standby"], "EXPECT_GRID_ARTWORK six visible semantic icons PASS", None),
    ("artwork_printing", 2, ["@expectgridicons", "@expecticon:nozzle:32", "@expecticon:bed:32", "@capture:qa_artwork_printing"], "EXPECT_GRID_ARTWORK six visible semantic icons PASS", None),
    ("artwork_paused", 3, ["@expectgridicons", "@capture:qa_artwork_paused"], "EXPECT_GRID_ARTWORK six visible semantic icons PASS", None),
    ("artwork_toolhead", 4, ["@expectbuttonicon:right:32:Y +", "@expectbuttonicon:right:32:X +", "@expectbuttonicon:home:32:X", "@expectbuttonicon:right:32:Z +", "@expectbuttonicon:left:32:Z -", "@expectbuttonicon:cool:32:ENFRIAR", "@expectbuttonicon:motors_off:32:Desactivar motores", "@expectnoicon:back", "@capture:qa_artwork_toolhead"], "EXPECT_BUTTON_ARTWORK Desactivar motores icon=motors_off size=32 text_clear=1 PASS", "CALL gcode"),
    ("artwork_settings_audio", 8, ["@expecticon:audio:32", "@expectbuttonicon:wifi:32:WI-FI Y MOONRAKER", "@expectnot:Uicons by Flaticon  |  www.flaticon.com/uicons", "@expectnot:Uicons by Flaticon", "@switch:0:0", "@tick", "@expecticon:mute:32", "@capture:qa_artwork_settings"], "ARTWORK mute", "CALL gcode"),
    ("artwork_startup_logos", 0, ["@expectlogos", "@expectbuttonicon:settings:32:AJUSTES", "@expect:KLIPPER_JC4880P443",
      "@expect:Versión: Simulador\nAutor: Demorian_Omega", "@expectnot:WIFI", "@expectnot:Uicons by Flaticon", "@capture:qa_artwork_startup_logos"],
     "EXPECT_APPROVED_LOGOS visible_without_recolor PASS", None),
    ("startup_history", 0,
     ["@tick", "@tick", "@expect:Wi-Fi: esperando IP\nMoonraker: esperando Wi-Fi\nKlipper: esperando Moonraker",
      "@screen:screen show startup", "@tick", "@network:reconnect", "@tick",
      "@expect:Wi-Fi: esperando IP\nMoonraker: esperando Wi-Fi\nKlipper: esperando Moonraker\nWi-Fi: IP 192.168.1.80\nMoonraker: conectado\nKlipper: listo"],
     "Klipper: listo PASS", "CALL gcode"),
    ("artwork_error", 9, ["@expectbuttonicon:refresh:64:REINICIAR KLIPPER", "@expectbuttonicon:power:64:APAGAR EQUIPO\nANFITRIÓN", "@expectbuttonicon:settings:32:AJUSTES", "@capture:qa_artwork_error"], "EXPECT_BUTTON_ARTWORK APAGAR EQUIPO\nANFITRIÓN icon=power size=64 text_clear=1 PASS", "CALL gcode"),
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
    ("last_job_reset_preserved", 1, ["@thumbnail:300", "@lastjob", "@tick", "@show:1", "ÚLTIMO TRABAJO", "@expect:COMPLETADO", "@expectimage:300:300", "LIMPIAR ESTADO Y VOLVER A ESPERA", "@tick", "@expectlastjob", "ÚLTIMO TRABAJO", "@expect:COMPLETADO", "@expectimage:300:300", "@advance:3000", "@capture:qa_last_job_after_reset"], "CALL reset_job 42", "CALL print"),
    ("last_job_current_file_cleared", 1, ["@thumbnail:300", "@lastjob", "@clearcurrent", "@tick", "ÚLTIMO TRABAJO", "@expect:COMPLETADO", "@expectimage:300:300", "@expectlastjob"], "EXPECT_LAST_JOB preserved=1", "CALL reset_job"),
    ("macro_hidden_prefix", 6, ["@hiddenmacros", "@tick", "@expectnotbutton:_INTERNAL_HELPER", "@expectnotbutton:__PRIVATE_START", "@expect:CLEAN_NOZZLE", "@show:6", "@expectnot:_INTERNAL_HELPER", "@expectnot:__PRIVATE_START", "@expectbuttondisabled:EJECUTAR", "@forceclick:EJECUTAR", "@capture:qa_macros_filtered"], "EXPECT_ABSENT_BUTTON _INTERNAL_HELPER PASS", "CALL macro"),
    ("network_offline_access", 8, ["@network:offline", "@tick", "WI-FI Y MOONRAKER", "@expectnetwork:1", "@expect:RED Y MOONRAKER", "@capture:qa_network_offline", "Cerrar", "@expectnetwork:0"], "EXPECT_NETWORK open=1 PASS", "CALL gcode"),
    ("network_scan_async", 8, ["WI-FI Y MOONRAKER", "Escanear", "@expect:Solicitud pendiente...", "@netcomplete:ok", "@tick", "@dropdown:0:1", "@expect:Red seleccionada: introduce la clave", "@capture:qa_network_scan"], "CALL network_scan", "CALL gcode"),
    ("network_busy_mailbox", 8, ["WI-FI Y MOONRAKER", "Escanear", "Escanear", "@expect:Red iniciando u ocupada; espera y vuelve a intentar", "@netcomplete:ok", "@tick", "@expect:Operacion completada"], "CALL network_scan", "CALL gcode"),
    ("network_wifi_save", 8, ["WI-FI Y MOONRAKER", "Escanear", "@netcomplete:ok", "@tick", "@dropdown:0:1", "@textarea:1:fixture-secret", "Guardar y conectar Wi-Fi", "@expectpasswordempty", "@expectclip:network:0", "@netcomplete:ok", "@tick", "@expectclip:network:1"], "CALL network_wifi ssid=Aperture Lab secured=1 password_redacted=1", "fixture-secret"),
    ("network_test_error_reconnect", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@textarea:0:192.168.1.121", "@textarea:1:7125", "Probar conexión", "@expect:Solicitud pendiente...", "@netcomplete:error", "@tick", "@expect:Error de conexion; vuelve a intentar", "@expectclip:network:1", "@capture:qa_network_error", "Probar conexión", "@netcomplete:ok", "@network:reconnect", "@tick", "@expect:Operacion completada", "@expectclip:network:2", "@capture:qa_network_reconnect"], "CALL network_test host=192.168.1.121 port=7125", "CALL gcode"),
    ("network_endpoint_save", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@textarea:0:192.168.1.121", "@textarea:1:7125", "Guardar Moonraker", "@netcomplete:ok", "@tick", "@expectnetwork:0", "@expectclip:network:1"], "CALL network_endpoint host=192.168.1.121 port=7125", "CALL gcode"),
    ("network_numeric_ip_keyboard", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@focus:0", "@expectkeyboard:1", "@expectnumericdots", "@textarea:0:192.168.1.121", "@capture:qa_network_numeric_ip", "@keyboarddone", "@focus:1", "@expectnumericdots", "@keyboarddone", "Cerrar"], "EXPECT_NUMERIC_DOTS PASS", "CALL network_"),
    ("network_validation", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@textarea:0:http://bad/path", "Probar conexión", "@expect:Revisa SSID, clave (8-63 bytes o 64 hex), IPv4 y puerto (1-65535)"], "EXPECT_LABEL Revisa SSID", "CALL network_test"),
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
     ["IMPRIMIR", "@expectview:1", "@expectstate:2", "@expectnot:IMPRIMIENDO", "@expect:Iniciando impresión…",
      "@expectcore:base", "@capture:qa_print_queued", "@printresult:0", "@tick", "@expectnot:Orden en espera",
      "@expectview:1", "@expectstate:2", "@expectprints:1"],
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
     ["IMPRIMIR", "@forceclick:Control manual", "@expectnot:Movimiento", "@expectprints:1"],
     "EXPECT_ABSENT Movimiento PASS", "CALL gcode"),
    ("move_external_print_blocks_jog", 4,
     ["@savebutton:X +", "@state:3", "@clicksaved", "@tick", "@expectview:2", "@clicksaved"],
     "EXPECT_VIEW 2 expected=2 PASS", "CALL gcode"),
    ("tuning_action_printing", 2,
     ["@expectnotbutton:TOOLHEAD", "@expectbuttonicon:settings:64:AJUSTES DE IMPRESIÓN", "AJUSTES DE IMPRESIÓN",
      "@expecttop:AJUSTES DE IMPRESIÓN", "@expecttuningvalue:0:100 %", "@expecttuningvalue:1:100 %", "@expecttuningvalue:2:50 %",
      "@capture:qa_tuning_printing", "CERRAR", "@expectnottop:AJUSTES DE IMPRESIÓN", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_action_paused", 3,
     ["@expectnotbutton:TOOLHEAD", "@expectbuttonicon:settings:64:AJUSTES DE IMPRESIÓN", "AJUSTES DE IMPRESIÓN",
      "@capture:qa_tuning_paused", "CERRAR", "@expectview:3", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_drafts_close_without_send", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@tuningclick:1:minus", "@tuningclick:2:plus",
      "@expecttuningvalue:0:105 %", "@expecttuningvalue:1:95 %", "@expecttuningvalue:2:55 %", "CERRAR",
      "AJUSTES DE IMPRESIÓN", "@expecttuningvalue:0:100 %", "@expecttuningvalue:1:100 %", "@expecttuningvalue:2:50 %",
      "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_speed_apply_once", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@tuningclick:0:apply", "@tuningforce:0:apply", "@expecttuningcalls:1:0:0",
      "@commandresult:1", "@tick", "@expectclip:tuning:1"],
     "CALL tuning control=0 percent=105 generation=1 job=42", "CALL gcode"),
    ("tuning_flow_apply", 3,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:1:minus", "@tuningclick:1:apply", "@expecttuningcalls:0:1:0",
      "@commandresult:1", "@tick", "@expectclip:tuning:1"],
     "CALL tuning control=1 percent=95 generation=1 job=42", "CALL gcode"),
    ("tuning_fan_apply", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:2:plus", "@tuningclick:2:apply", "@expecttuningcalls:0:0:1",
      "@commandresult:1", "@tick", "@expectclip:tuning:1"],
     "CALL tuning control=2 percent=55 generation=1 job=42", "CALL gcode"),
    ("tuning_fan_missing", 2,
     ["@tuningvalue:2:0:0", "@tick", "AJUSTES DE IMPRESIÓN", "@expecttuningvalue:2:-- %",
      "@expecttuningdisabled:2:plus", "@expecttuningdisabled:2:minus", "@expecttuningdisabled:2:apply",
      "@tuningforce:2:apply", "@expecttuningcalls:0:0:0", "@capture:qa_tuning_fan_missing"], None, "CALL tuning"),
    ("tuning_fan_out_of_physical_range", 2,
     ["@tuningvalue:2:1:999", "@tick", "AJUSTES DE IMPRESIÓN", "@expecttuningdisabled:2:apply",
      "@tuningforce:2:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_live_actual_preserves_draft", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@tuningvalue:0:1:90", "@tick",
      "@expecttuningvalue:0:90 %", "@expecttuningvalue:0:105 %", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_validity_change_closes_modal", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningvalue:2:0:0", "@tick", "@expectnottop:AJUSTES DE IMPRESIÓN", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_pause_transition_closes_modal", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@state:4", "@tick", "@expectnottop:AJUSTES DE IMPRESIÓN",
      "@expectview:3", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_endpoint_changes_before_apply", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@generation:2", "@tuningforce:0:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_job_changes_before_apply", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:1:plus", "@jobid:43", "@tuningforce:1:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_finished_before_apply", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@state:5", "@tuningforce:0:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_offline_before_apply", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:2:plus", "@network:offline", "@tuningforce:2:apply", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("inspection_tuning_blocks_open", 2,
     ["@screen:screen show paused", "@tick", "@forceclick:AJUSTES DE IMPRESIÓN", "@expectnottop:AJUSTES DE IMPRESIÓN", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("inspection_tuning_closes_modal", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@screen:screen show settings", "@tick",
      "@expectnottop:AJUSTES DE IMPRESIÓN", "@expectinspection:1:8:0", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
    ("tuning_unknown_shields_controls", 2,
     ["AJUSTES DE IMPRESIÓN", "@tuningclick:0:plus", "@resultunknown", "@tick", "@expect:RESULTADO INCIERTO",
      "@expectnottop:AJUSTES DE IMPRESIÓN", "@forceclick:AJUSTES DE IMPRESIÓN", "@expectnottop:AJUSTES DE IMPRESIÓN", "@expecttuningcalls:0:0:0"], None, "CALL tuning"),
]

# Surface and navigation checks exercise the actual renderer and pointer input.
for view in range(11):
    CASES.append((f"visual_surface_{view:02}", view, ["@expectlayout", "@expectlaboratory"], "EXPECT_LAYOUT headerless=1", "CALL gcode"))
for name, start, route, secondary, back in (
        ("toolhead", 1, "Control manual", 4, 1), ("files", 1, "IMPRIMIR", 5, 1),
        ("macros", 1, "MACROS", 6, 1), ("settings", 1, "AJUSTES", 8, 1),
        ("details", 1, "ÚLTIMO TRABAJO", 10, 1), ("objects", 2, "EXCLUIR", 7, 2),
        ("printing_macros", 2, "MACROS", 6, 2), ("error_settings", 9, "AJUSTES", 8, 9),
        ("startup_settings", 0, "AJUSTES", 8, 0)):
    CASES.append((f"visual_swipe_{name}", start,
                  [route, f"@expectview:{secondary}", "@swipe:18:24", f"@expectview:{back}"],
                  "SWIPE_RIGHT pointer_samples=7", "CALL gcode"))
CASES += [
    ("mosaic_keyboard_symbols", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@keyboardmode:2", "@expectkeyboard:1", "@capture:qa_network_keyboard_symbols", "@keyboardmode:1", "@capture:qa_network_keyboard_upper", "@keyboarddone", "Cerrar"], None, "CALL network_"),
    ("mosaic_background_swipe", 1, ["Control manual", "@swipe:310:24", "@expectview:1"], "SWIPE_RIGHT", "CALL gcode"),
    ("mosaic_tuning_icons", 2, ["AJUSTES DE IMPRESIÓN", "@expectlaboratory", "@capture:qa_tuning_surface", "CERRAR"], "EXPECT_LABORATORY", "CALL tuning"),
    ("mosaic_confirm_capture", 2, ["CANCELAR", "@expectcore:warning", "@capture:qa_confirm_cancel", "VOLVER"], None, "CALL cancel"),
    ("mosaic_emergency_capture", 1, ["PARADA", "@capture:qa_confirm_emergency", "VOLVER"], None, "CALL emergency"),
    ("visual_swipe_files_row", 1, ["IMPRIMIR", "@swipe:44:90", "@expectview:1"], None, "CALL print"),
    ("visual_swipe_macros_row", 1, ["MACROS", "@swipe:44:90", "@expectview:1"], None, "CALL macro"),
    ("visual_completed_content", 1, ["@state:5", "@tick", "@expect:Completado", "@expectlayout", "@capture:qa_state_complete"], None, "CALL gcode"),
    ("visual_cancelled_content", 1, ["@state:6", "@tick", "@expect:Cancelado", "@expectlayout", "@capture:qa_state_cancelled"], None, "CALL gcode"),
    ("visual_disconnected_content", 1, ["@network:offline", "@tick", "@expect:DESCONECTADO", "@expectcore:offline", "@expectlayout", "@capture:qa_state_disconnected"], None, "CALL gcode"),
    ("visual_wifi_keyboard_cancel", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@expectkeyboard:1", "@expectlayout", "@keyboardcancel", "@expectkeyboard:0", "@expectnetwork:1", "Cerrar", "@expectnetwork:0"], None, "CALL network_"),
    ("visual_cancel_modal_swipe", 2, ["CANCELAR", "@expectlayout", "@swipe:18:240", "@expect:CANCELAR IMPRESIÓN", "VOLVER", "@expectnot:CANCELAR IMPRESIÓN", "@expectview:2", "@capture:qa_cancel_closed"], None, "CALL gcode"),
    ("visual_keypad_surface", 4, ["210 °C", "@expectlayout", "@expectsingleline:BORRAR", "@capture:qa_target_keypad", "@swipe:18:240", "@expect:OBJETIVO - BOQUILLA", "VOLVER", "@expectnot:OBJETIVO - BOQUILLA"], None, "CALL gcode"),
    ("visual_tuning_surface", 2, ["AJUSTES DE IMPRESIÓN", "@expectlayout", "@capture:qa_tuning_surface", "@swipe:5:240", "@expecttop:AJUSTES DE IMPRESIÓN", "CERRAR", "@expectnottop:AJUSTES DE IMPRESIÓN"], None, "CALL tuning"),
    ("visual_moonraker_surface", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@expectlayout", "@capture:qa_moonraker_surface", "Cerrar", "@expectnetwork:0"], None, "CALL network_"),
    ("visual_movement_capture", 4, ["@expectlayout", "@capture:qa_movement"], None, "CALL gcode"),
    ("visual_objects_capture", 7, ["cube_2", "@expectlayout", "@capture:qa_objects"], None, "CALL exclude"),
]

# Pointer paths use the hardware's 20 ms sampling period, including movements
# below LVGL's native gesture velocity threshold. Selection is checked on the
# preview title; write counts ensure a slider commits exactly once on release.
CASES += [
    ("gesture_file_tap", 1, ["IMPRIMIR", "@drag:100:180:3:2:2:20", "@expectview:5", "@expectselectedfile:1"],
     "CALL fetch_metadata portal_turret.gcode", "CALL print"),
    ("gesture_file_short_drag", 1, ["IMPRIMIR", "@drag:100:180:45:0:30:20", "@expectview:5", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_slow_back", 1, ["IMPRIMIR", "@drag:100:180:120:0:120:20", "@expectview:1", "IMPRIMIR", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_horizontal_diagonal", 1, ["IMPRIMIR", "@drag:100:180:120:35:60:20", "@expectview:1"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_vertical_diagonal", 1, ["IMPRIMIR", "@drag:100:180:50:100:50:20", "@expectview:5", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_vertical_then_right", 1, ["IMPRIMIR", "@pointer:1:100:180:20", "@pointer:1:100:220:20",
     "@pointer:1:250:220:20", "@pointer:0:250:220:20", "@expectview:5", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_scroll", 1, ["IMPRIMIR", "@manyfiles", "@tick", "@drag:100:180:10:-100:50:20",
     "@expectview:5", "@expectfilescrolled", "@expectselectedfile:0"], None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_scroll_then_right", 1, ["IMPRIMIR", "@manyfiles", "@tick", "@pointer:1:100:180:20",
     "@pointer:1:100:140:20", "@pointer:1:250:140:20", "@pointer:0:250:140:20",
     "@expectview:5", "@expectfilescrolled", "@expectselectedfile:0"], None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_out_and_back", 1, ["IMPRIMIR", "@pointer:1:100:180:20", "@pointer:1:200:180:20",
     "@expectview:5", "@pointer:1:100:180:20", "@pointer:0:100:180:20", "@expectview:5", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_file_left_drag", 1, ["IMPRIMIR", "@drag:200:180:-120:0:60:20", "@expectview:5", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_back_on_release", 1, ["IMPRIMIR", "@pointer:1:100:180:20", "@pointer:1:250:180:20", "@expectview:5",
     "@pointer:0:250:180:20", "@expectview:1", "@pointer:0:250:180:20", "@expectview:1"], None, "CALL print"),
    ("gesture_below_threshold", 1, ["IMPRIMIR", "@drag:100:180:79:0:79:20", "@expectview:5", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_at_threshold", 1, ["IMPRIMIR", "@drag:100:180:80:0:80:20", "@expectview:1"], None, "CALL print"),
    ("gesture_diagonal_no_back", 1, ["IMPRIMIR", "@drag:100:180:100:60:20:20", "@expectview:5", "@expectselectedfile:0"],
     None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_drag_then_tap", 1, ["IMPRIMIR", "@drag:100:180:45:0:30:20", "@expectselectedfile:0",
     "@drag:100:180:0:0:1:20", "@expectview:5", "@expectselectedfile:1"], "CALL fetch_metadata portal_turret.gcode", "CALL print"),
    ("gesture_view_change_cancels_contact", 1, ["IMPRIMIR", "@pointer:1:100:180:20", "@pointer:1:145:180:20",
     "@show:1", "@pointer:0:145:180:20", "@expectview:1"], None, "CALL fetch_metadata portal_turret.gcode"),
    ("gesture_modal_guard", 1, ["PARADA", "@drag:50:240:120:0:120:20", "@expect:PARADA DE EMERGENCIA",
     "@expectview:1", "VOLVER"], None, "CALL emergency"),
    ("gesture_network_guard", 8, ["WI-FI Y MOONRAKER", "@drag:18:24:120:0:120:20", "@expectnetwork:1", "@expectview:8", "Cerrar"],
     None, "CALL network_"),
    ("gesture_inspection_guard", 1, ["@screen:screen show files", "@tick", "@drag:100:180:120:0:120:20",
     "@expectinspection:1:5:0", "@expectview:5"], None, "CALL print"),
    ("gesture_primary_drag_no_action", 1, ["@drag:400:80:120:0:60:20", "@expectview:1"], None, "CALL fetch_files"),
]
for name, x, y, dx, dy, steps, expected_writes in (
        ("brightness_right", 100, 100, 150, 0, 5, "1:0"),
        ("brightness_slow", 100, 100, 120, 0, 120, "1:0"),
        ("brightness_left", 260, 100, -160, 0, 80, "1:0"),
        ("brightness_outside", 100, 100, 350, 160, 80, "1:0"),
        ("brightness_touch_margin", 100, 118, 120, 0, 60, "1:0"),
        ("volume_right", 500, 158, 150, 0, 5, "0:1"),
        ("volume_slow", 500, 158, 120, 0, 120, "0:1"),
        ("volume_left", 700, 158, -150, 0, 75, "0:1"),
        ("volume_outside", 500, 158, 200, 150, 100, "0:1")):
    CASES.append((f"gesture_{name}", 1, ["AJUSTES", f"@drag:{x}:{y}:{dx}:{dy}:{steps}:20", "@expectview:8",
                  f"@expectwrites:{expected_writes}", "@tick", "@drag:40:280:120:0:120:20", "@expectview:1"], None, "CALL gcode"))
for name, index, x, y, moved_x, expected_label in (
        ("brightness", 0, 100, 100, 200, "80 %"), ("volume", 1, 500, 158, 620, "50 %")):
    CASES.append((f"gesture_{name}_cancel", 1, ["AJUSTES", f"@pointer:1:{x}:{y}:20", f"@pointer:1:{moved_x}:{y}:20",
                  "@expectwrites:0:0", "@screen:screen show settings", "@tick", f"@pointer:0:{moved_x}:{y}:20",
                  "@screen:screen live", "@tick", "AJUSTES", f"@expect:{expected_label}", "@expectwrites:0:0",
                  f"@drag:{x}:{y}:50:0:25:20", "@expectview:8", "@expectwrites:" + ("1:0" if index == 0 else "0:1")], None, "CALL gcode"))

for name, _, actions, _, _ in CASES:
    if name.startswith("gesture_"):
        # An accepted brightness save does not itself write flash.
        for at in range(len(actions)-1,-1,-1):
            if actions[at].startswith("@expectwrites:1:"):
                actions.insert(at,"@displaycomplete:ok")

CASES += [
    ("performance_brightness_coalesced", 8, ["@slider:55", "@expectdisplaypending:1", "@expectwrites:0:0",
     "@slider:70", "@dropdown:0:1", "@expectwrites:0:0", "@displaycomplete:ok", "@expectdisplaypending:0",
     "@expectwrites:1:0", "@tick", "@expect:70 %"], "CALL settings_persist brightness=70 sleep=5", "CALL gcode"),
    ("performance_brightness_failure_retry", 8, ["@slider:55", "@displaycomplete:error", "@tick",
     "@expect:No se pudieron guardar los ajustes de pantalla", "@expectwrites:1:0", "@slider:55", "@expectdisplaypending:1",
     "@displaycomplete:ok", "@tick", "@expectwrites:2:0"], "CALL settings_persist brightness=55 sleep=10 result=0", "CALL gcode"),
    ("performance_brightness_dirty", 8, ["@slider:55", "@expectdamage:20", "@expectdirectframe"], "EXPECT_DIRECT_FRAME matches_full_redraw=1 PASS", "CALL gcode"),
    ("performance_volume_dirty", 8, ["@slider:1:75", "@expectdamage:20", "@expectdirectframe"], "EXPECT_DIRECT_FRAME matches_full_redraw=1 PASS", "CALL gcode"),
    ("performance_settings_idle", 8, ["@expectidleframes", "@expectdirectframe"], "EXPECT_IDLE_FRAMES extra=0 PASS", "CALL gcode"),
    ("performance_standby_idle", 1, ["@expectidleframes", "@expectdirectframe"], "EXPECT_IDLE_FRAMES extra=0 PASS", "CALL gcode"),
    ("performance_files_selection_pixels", 5, ["portal_turret.gcode", "@expectdirectframe", "calibration_cube.gcode", "@expectdirectframe"], None, "CALL print"),
    ("performance_modal_pixels", 1, ["PARADA", "@expectdirectframe", "VOLVER", "@expectdirectframe"], None, "CALL emergency"),
    ("performance_network_keyboard_pixels", 8, ["WI-FI Y MOONRAKER", "@focus:0", "@expectdirectframe", "@keyboarddone",
     "@expectdirectframe", "Cerrar", "@expectdirectframe"], None, "CALL network_"),
    ("saver_touch_wakes_without_pausing", 2,
     ["@advance:600001", "@expectsaver:IMPRIMIENDO", "@expectsaver:63 %", "@expectsaver:59 min",
      "@expectsaver:1 h 44 min", "@expectsaver:Boquilla  207 °C", "@expectbrightness:25",
      "@capture:qa_saver_printing", "@expecteyemove", "@expectdirectframe",
      "@pointer:1:440:85:20", "@pointer:0:440:85:20", "@expectnosaver", "@expectbrightness:80",
      "@expectidleframes"], "EXPECT_BRIGHTNESS 80 PASS", "CALL gcode"),
    ("saver_next_touch_works", 2,
     ["@advance:600001", "@pointer:1:440:85:20", "@pointer:1:670:82:800", "@pointer:0:670:82:20",
      "@expectnosaver", "@expectnot:CONFIRMAR", "@pointer:1:440:85:20", "@pointer:0:440:85:20"],
     "CALL gcode PAUSE", "CALL gcode CANCEL_PRINT"),
    ("saver_states_keep_moving", 2,
     ["@advance:600001", "@state:4", "@tick", "@expectsaver:EN PAUSA", "@expecteyemove",
      "@capture:qa_saver_paused", "@state:5", "@tick", "@expectsaver:COMPLETADO", "@expectsaver:100 %",
      "@expecteyemove", "@capture:qa_saver_complete", "@state:6", "@tick", "@expectsaver:CANCELADO",
      "@expecteyemove", "@capture:qa_saver_cancelled", "@state:7", "@tick", "@expectsaver:ERROR DE KLIPPER",
      "@expectsaver:Extrude below minimum temp: revisa el hotend antes de continuar",
      "@expecteyemove", "@capture:qa_saver_error", "@expectbrightness:25",
      "@pointer:1:402:390:20", "@pointer:0:402:390:20", "@expectnosaver", "@expectview:9"],
     "EXPECT_SAVER ERROR DE KLIPPER PASS", "CALL gcode"),
    ("saver_respects_lower_brightness", 1,
     ["@networkbrightness:10", "@tick", "@advance:600001", "@expectsaver:REPOSO", "@expectbrightness:10",
      "@networkbrightness:90", "@tick", "@expectbrightness:25", "@pointer:1:680:390:20",
      "@pointer:0:680:390:20", "@expectbrightness:90", "@expectnosaver", "@expectnot:CONFIRMAR"],
     "EXPECT_BRIGHTNESS 90 PASS", "CALL emergency"),
    ("saver_connection_loss", 2,
     ["@advance:600001", "@network:offline", "@tick", "@expectsaver:DESCONECTADO",
      "@expectbrightness:25", "@network:reconnect", "@tick", "@expectsaver:REPOSO",
      "@expectbrightness:25", "@pointer:1:10:10:20", "@pointer:0:10:10:20", "@expectbrightness:80"],
     "EXPECT_SAVER DESCONECTADO PASS", "CALL gcode"),
    ("saver_yields_to_inspection", 2,
     ["@advance:600001", "@expectbrightness:25", "@screen:screen show settings", "@tick",
      "@expectinspection:1:8:0", "@expectnosaver", "@expectbrightness:80"], "EXPECT_BRIGHTNESS 80 PASS", "CALL gcode"),
]

# Repeated real button events exercise the boundaries without test-only UI hooks.
for control, minimum, maximum, initial in ((0, 10, 200, 100), (1, 50, 150, 100), (2, 0, 100, 50)):
    for direction, boundary in (("minus", minimum), ("plus", maximum)):
        CASES.append((f"tuning_limit_{control}_{direction}", 2,
                      ["AJUSTES DE IMPRESIÓN"] + [f"@tuningforce:{control}:{direction}"] * 45
                      + [f"@expecttuningvalue:{control}:{boundary} %", f"@tuningclick:{control}:apply",
                         "@expecttuningcalls:" + ":".join("1" if i == control else "0" for i in range(3))],
                      f"CALL tuning control={control} percent={boundary} generation=1 job=42", "CALL gcode"))
for control in range(3):
    for malformed in ("nan", "inf", "-1"):
        CASES.append((f"tuning_invalid_{control}_{malformed}", 2,
                      [f"@tuningvalue:{control}:1:{malformed}", "@tick", "AJUSTES DE IMPRESIÓN",
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
        encoded = [
            action if action.isascii() and action.startswith("@") else
            ("@utf8:" if action.startswith("@") else "hex:") + action.encode("utf-8").hex()
            for action in actions
        ]
        run = subprocess.run([str(EXE), str(OUT), str(view)] + encoded, capture_output=True,
                             text=True, encoding="utf-8", errors="replace", timeout=30)
        (logs / (name + ".log")).write_text(run.stdout + run.stderr, encoding="utf-8")
        activity = run.stdout.split("ACTION_BEGIN\n", 1)[-1]
        success = run.returncode == 0 and (required is None or required in activity) and (forbidden is None or forbidden not in activity)
        if name.startswith("voices_") and re.search(r"^CALL (?:gcode|print|macro|exclude|emergency|shutdown|reset_job|network_[a-z]+)\b", activity, re.M):
            success = False
        if name.startswith("inspection_") and name != "inspection_resume_controls" and re.search(
                r"^CALL (?:gcode|print|macro|exclude|emergency|shutdown|reset_job|network_[a-z]+|settings(?:_submit|_persist)?|audio_enabled|audio_volume)\b", activity, re.M):
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
