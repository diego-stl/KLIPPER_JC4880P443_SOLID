# Audio service

The single worker owns ES8311/I2S operations. Queue capacity is eight requests,
shared FIFO between the existing diagnostic tone and the seven original voices.
There are no priorities, automatic boot sounds or Klipper event subscriptions in
this component. The application decides when to call `audio_service_play()`.
All requests, including wakeup and manual console playback, respect ready,
enabled, nonzero volume and cancellation generation. OFF/STOP immediately lower
the manually owned PA signal, invalidate active work and reset pending requests.
Every new request clears the complete DMA ring before enabling audible output.

NVS `jc_audio/settings_v1` remains unchanged: default enabled=true, volume=50;
the currently saved user volume is preserved. No idle-DAC, PWM or volume-curve
changes are included in the voice integration. Settings persist asynchronously
in the worker; `settings_pending`/`AUDIO_SETTINGS_SAVED` identify completion.

The public enum and clip names are wakeup, printing, pause, cancel, error,
complete and standby. `last_clip` is `AUDIO_CLIP_COUNT` until a voice actually
starts. Voice counters are independent from diagnostic-tone counters.

Console keeps all previous commands and adds `audio sounds` and
`audio play NAME`. A successful play response acknowledges queueing, not sound
completion. `AUDIO_CLIP_START`, `AUDIO_CLIP_DONE` and `AUDIO_CLIP_ABORT` identify
the actual worker activity. No printer command is sent by audio playback.

## Resource provenance and format

`assets/provenance.json` records source paths, source-header SHA256, original-WAV
SHA256, chunk sizes and durations. Resources came from the user's existing
`KLIPPER_ES3C28P_SOLID/src/services/glados_*.h`. The WAV bytes are unchanged;
only Arduino-specific declaration syntax was adapted for const Flash arrays.
No license is fabricated: the original user-provided headers have no explicit
asset license statement.

All seven WAVs are PCM format1, mono, 16-bit little endian, 22050Hz. Container
bytes total640218; PCM bytes total639168. Playback locates and validates RIFF
chunks and uses only `data`, excluding each file's final106 bytes of LIST/id3
metadata. Every mono sample is copied unchanged to L/R and duplicated in time,
producing stereo44100Hz as in the original implementation. The conversion uses
small fixed stack blocks and byte-wise LE reads, avoiding alignment assumptions.

| Clip | WAV bytes | PCM bytes | PCM duration |
|---|---:|---:|---:|
| wakeup |96918|96768|2.194s|
| printing |75134|74984|1.700s|
| pause |42774|42624|0.967s|
| cancel |260898|260748|5.913s|
| error |68206|68056|1.543s|
| complete |60054|59904|1.358s|
| standby |36234|36084|0.818s|

`audio_logic_regression.c` tests the actual console parser, cancellation
eligibility and settings decoder. `audio_wav_regression.c` tests every real WAV
and all1278336 expanded samples, container truncations, invalid formats,
duplicate chunks, odd padding, chunk-order independence and output guard bounds.
`verify_asset_provenance.py` checks SHA256 and byte identity against the original
headers when their recorded source directory is available.

The local C tests pass with TinyCC `-Wall -Werror -run` and require no COM port,
printer or audio hardware. Root performs the ESP-IDF build and physical checks;
native tests alone do not establish audible quality or physical cancellation.

## Voice amplitude adaptation after hardware test

The first full-scale wakeup voice at the saved volume66 triggered the P4
brownout detector during the physical0.4 test. Root retains that evidence in
`verification/audio-voices/03-device-voices.log`. This does not establish every
electrical cause of the supply drop, and software gain alone cannot certify
the power supply or speaker operating margin.

Voice playback applies a separate integer3/10 gain in32-bit arithmetic to each
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
