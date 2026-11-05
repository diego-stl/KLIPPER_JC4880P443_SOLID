# Portal firmware host renderer

Compiles the production Portal UI (including the network settings overlay),
flash assets, local LVGL 9.3 and real `audio_events` controller with TinyCC.
Device/network/audio worker/Klipper services are explicit mocks in
`portal_host.c`. No serial ports, Moonraker requests or physical audio are
used. These checks validate widget rendering and callback wiring; physical
display, touch and printer behavior still need hardware validation.

From the project root, using Python with Pillow:

```powershell
python tools/render_portal_firmware.py
python tests/portal_ui_regression.py
```

`--tcc PATH` overrides the portable compiler; `--output PATH` selects the
artifact directory (default `tests/portal_host/output`). `--skip-build`
requires exact source fingerprints. Windows sandbox restrictions may cause
WinError 623 when loading TinyCC or its executable; the local simulator must
then run with permission to load those DLLs.

The renderer emits eleven 800 × 480 RGB565 frames and PNGs, a contact sheet,
compiler/check logs and source hashes. `CLIP` means a label extends beyond
the screen without an on-screen scrolling ancestor to clip it. Intentionally
scrollable content is checked as a viewport rather than treated as a screen
layout failure. Screenshots remain the visual review evidence.

`build_sources.json` binds the executable to all UI sources, relevant public
and private headers (Portal/assets/network/Klipper/board/audio), host shims,
fixtures and the renderer. A stale binary cannot run a regression. Generated
`fixtures_png.h` contains deterministic synthetic transparent PNGs at 100
and 300 pixels; both pass through the real LVGL LodePNG decoder. They do not
add assets to firmware. The host `esp_heap_caps.h` shim maps PSRAM requests
to desktop allocation and makes no claim about ESP heap capacity.

## Regression coverage

Print acceptance leaves Files immediately and displays the last real printer
state while awaiting WebSocket confirmation. Tests cover pending, completed,
rejected and UNKNOWN responses, stale repeated touches, pause transitions,
and attempts to enter movement or jog after a print begins externally.

Printing and paused grids use the settings artwork for AJUSTES IMPRESION.
The real three-row overlay is exercised through its LVGL buttons: opening,
closing and editing drafts send no commands; each Apply targets only its
speed, flow or layer-fan control, binds endpoint/job identity and sends once
while pending. Boundary tests clamp at 10–200%, 50–150% and 0–100%.
Missing/nonfinite/negative values, fan values over 100%, changed validity,
job/state/endpoint changes, inspection and UNKNOWN overlays cannot dispatch
an adjustment. Screenshots include both live views, the modal and missing fan.

The suite preserves UNKNOWN result overlays across view changes, all audio
control and transition policies, and inspection read-only safeguards. New
cases cover direct print dispatch, repeated clicks while pending, direct
restart in error, changed job/state/endpoint guards, six equal action
buttons, long file names, one/two/ten/missing color sets, duplicate colors
from distinct tools, and an eight-slot Orca case with only T0/T1/T2/T7
consuming filament. The UI shows only their four color chips. Tests retain
last-job details after reset and after the current file is cleared, and
hidden macro prefixes. The original GLaDOS head must appear in standby,
complete, cancelled and error states; its lens color assertions remain active.

PNG assertions inspect the real LVGL image descriptors and rendered scales:
The G-Code list contains only filenames, without thumbnail cache APIs or
images. Exactly one selected RAW PNG appears on the Files screen. Both
100/300-pixel sources scale to 300 pixels; printing remains at 200 and
details at 300. A missing selected image shows NO DATA. The Files footer is
a text-only 48-pixel print button. File metadata quantities remain present
in the mocked backend and details, but are absent from the Files color strip.
Color-chip assertions inspect small opaque LVGL objects by their metadata
colors, including duplicate chips for different tools sharing one color.
`qa_*.png` captures these layouts, filament cases and filtered macro lists.

Network cases use an explicit asynchronous mailbox: acceptance marks the
mock busy; `@netcomplete:ok/error` completes it. Tests cover offline/error
access to settings, scanning, selected secure SSID, password clearing,
IPv4 validation, endpoint save, connection errors/retry, numeric IP/port
keyboards including the decimal point key, keyboard layout and
inspection guards. Teardown regressions close the overlay with its keyboard
visible or hidden while inspection is pending, and during endpoint changes,
UNKNOWN results and ordinary close actions. These execute real LVGL DELETE
callbacks to cover the reentrant teardown failure. Textarea values and Wi-Fi passwords are redacted in mock
logs. `qa_network_*.png` captures the real overlay, including its keyboard.

The last-job reset mock emits a COMPLETED `SDCARD_RESET_FILE` result, changes
the live state and clears its filename while retaining the last-job record
and metadata. The test reopens it using the actual standby button.

Audio routing uses the real event controller and a mock service. Voices are
counted without decoding PCM, DSP or speaker output. `voices_*` cases retain
an untouched controller baseline and cover all seven clips, silent initial
states, reconnect and endpoint invalidation, mute/zero-volume suppression
without replay, readiness and partial snapshots. They reject printer and
network writes. Inspection cases feed real console requests through the GUI
mailbox, keep snapshots truthful, reject callbacks behind the shield, hold
views across backend changes, suppress sleep, cancel action modals and
preserve uncertain-result notices. They also reject printer/network writes
and settings mutations.

## Host action interface

The executable accepts an output folder, initial view index and actions.
Plain strings match exact button labels. UTF-8 labels can use
`hex:<UTF-8 bytes>` to avoid Windows argv code pages. The Python regression
performs this encoding automatically.

Useful directives include `@tick`, `@show:N`, `@state:N`, `@generation:N`,
`@thumbnail:0/100/300`, `@tools:N/orca/same`, `@longname`, `@lastjob`,
`@clearcurrent`, `@expectlastjob`, `@expectimage:source:visual`,
`@expectthumbcount:N`, `@expectswatches:N`, `@expectcolor:#RRGGBB:count`,
`@scrollswatch:index`, `@expectcharacter:count`, `@expectfileslist`,
`@expectprintfooter`, `@expectgrid`,
`@expect:exact label`, `@expectnot:exact label`, `@capture:name`,
`@network:offline/error/reconnect`, `@netcomplete:ok/error`,
`@textarea:index:text`, `@dropdown:index:selection`, `@focus:index` and
`@keyboarddone`. Widget selection searches the overlay before the screen.
`@forceclick:label` deliberately sends a real event to a disabled button to
verify callback guards; ordinary clicks fail for disabled/absent buttons.

Action cases establish a live model before forcing the initial view, so the
first UI tick does not accidentally navigate away from a test page. Audio
baseline isolation then clears the mock queue before assertions. The final
`action_report.json` contains results, actual events, recorded mock calls,
clipping and exact source hashes; its scenario count is the authoritative
count for the current suite.
