# Audio service

The single worker owns ES8311/I2S operations and shares an eight-request FIFO
between the diagnostic tone and the clips. Playback requires ready audio,
sound enabled and nonzero volume. OFF/STOP immediately lower PA and clear
the queue. Printer state and command clips also cancel when their printer
source changes or disconnects. Network, system and wakeup clips can play
through those transitions. Every request clears the DMA ring before playback.

Settings are saved asynchronously in NVS `jc_audio/settings_v1`.
`settings_pending` and `AUDIO_SETTINGS_SAVED` report persistence.
Run `audio sounds` to list clips or `audio play NAME` to queue one.
`AUDIO_CLIP_START`, `AUDIO_CLIP_DONE` and `AUDIO_CLIP_ABORT` report playback.

## Clips del panel

| Clip | Recurso | Disparador |
|---|---|---|
| `wakeup` | `glados_wakeup.h` | Primer audio listo tras encender el panel o botón **Probar audio** |
| `printing` | `wpn_portalgun_activation_01.wav` | Klipper pasa a imprimiendo al iniciar o reanudar |
| `pause` | `laser_node_power_off.wav` | Impresión en curso pasa a pausada |
| `cancel` | `hackbuzzer.wav` | Klipper informa cancelación |
| `error` | `turret_disabled_2.wav` | Klipper pasa a error con el WebSocket abierto |
| `complete` | `aperture_logo_bells_01_01.wav` | Klipper informa impresión completada |
| `standby` | `wpn_portal_fizzler_shimmy_01.wav` | Klipper pasa a espera |
| `temperature` | `portal_enter_01.wav` | Orden de hotend, cama, perfil de material o enfriamiento confirmada |
| `tuning` | `portal_fizzle2.wav` | Orden de velocidad, flujo o ventilador de capa confirmada |
| `movement` | `interior_arm_platform_close_01.wav` | Orden de homing, movimiento de eje o desactivación de motores confirmada |
| `filament` | `retract.wav` | Orden de extrusión o retracción durante una pausa confirmada |
| `network` | `button_synth_positive_01.wav` | Guardado de Wi-Fi o Moonraker, o resultado de **Probar conexión** |
| `system` | `button_synth_negative_02.wav` | Solicitud de parada de emergencia, reinicio de Klipper o apagado del host encolada |

## Resource provenance and format

`assets/provenance.json` records the import of the seven original GLaDOS WAVs.
The original `wakeup` remains embedded. The twelve effects came from the user's
`elegidas` directory and were converted to PCM mono, signed 16-bit little endian
at 22,050 Hz. Stereo sources were averaged to mono, 44,100 Hz sources were
low-pass filtered before decimation, and unsigned 8-bit `retract` was expanded
to 16-bit at its original sample rate. CMake embeds the converted WAVs.

The player validates each RIFF container and uses its `data` chunk. It doubles
each mono frame in time and copies it to both stereo channels for 44,100 Hz I²S.
`audio_wav_regression.c` checks the WAVs and sample expansion. Run it from the
repository root so it can read the embedded source files.

## Voice amplitude adaptation after hardware test

The first full-scale wakeup voice at the saved volume66 triggered the P4
brownout detector during the physical0.4 test. The local archive retains that
evidence in `no nesesario/verification/audio-voices/03-device-voices.log`. This does not establish every
electrical cause of the supply drop, and software gain alone cannot certify
the power supply or speaker operating margin.

Clip playback applies an integer 3/10 gain in 32-bit arithmetic to each
converted block immediately before I2S. Maximum signed PCM amplitude is9830,
matching the30% envelope of the previously validated diagnostic tone. This
reduces peak amplitude approximately10.46dB from a full-scale voice. Original
WAV bytes, RIFF parsing and exact native expansion remain unchanged. The
volume curve, saved volume66, default50, brownout detector and screen/PWM
behavior remain unchanged. There is no compression or clipping; rounding
contributes less than one output LSB. The exhaustive gain test covers all65536
int16 values and guard boundaries.

`AUDIO_READY` reports `voice_gain_pct=30`; `AUDIO_CLIP_START` reports
`gain_pct=30`. Native tests do not prove that the physical brownout is resolved;
root must rebuild and repeat muted-boot and voice hardware checks.
