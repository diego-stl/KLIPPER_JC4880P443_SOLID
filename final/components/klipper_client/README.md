# Moonraker presentation contract

`snapshot.state` is the single state consumed by UI and audio. `raw_state` and
`pause_pending` remain available for action validation. A printing-to-paused
transition waits 2500 ms of monotonic time. Repeated updates do not restart that
clock; printing cancels it, and errors/complete/cancel bypass it. A paused
connection/reconnection snapshot establishes a visible paused baseline.

The client optionally discovers `gcode_macro TIMELAPSE_TAKE_FRAME` and queries
its `is_paused`/`takingframe` variables. Compatible bool or numeric 0/1 fields
enable a supplemental subscription, without delaying printer readiness. An
active indication observed within 2500 ms of the pending pause can extend the
filter to a maximum of 15 seconds from pause onset. An inactive indication
allows 1 second to return to printing, still inside that hard limit. A stuck
flag cannot suppress a pause indefinitely. Out-of-order WebSocket updates are
discarded before applying flags. With an absent/incompatible macro the 2500 ms
filter applies. These bounded heuristics cannot distinguish every timelapse
implementation from a real user pause.

## Print settings

`klipper_client_set_print_tuning` accepts a typed speed, flow or part-fan
adjustment with the displayed endpoint generation and job token. Speed is
10–200%, flow 50–150%, and fan 0–100%. It emits only M220, M221 or M106,
converting the fan percentage to a rounded 0–255 value. Queue and pre-body
guards reject stale jobs, states, connections, endpoints or unavailable readings.
UNKNOWN results retain the existing no-retry policy.

The subscription reads `gcode_move.speed_factor`, `extrude_factor` and optional
`fan.speed`. Snapshot percentages have separate validity flags; missing or
malformed data is never replaced with a guessed default. Partial deltas retain
known fields, while disconnects and endpoint changes invalidate them.
`panel status` exports `PANEL_TUNING` for read-only comparison with Moonraker.

## Printer-side macro visibility

Private names beginning `_` are always omitted before applying the 20 visible
macro limit. Existing public macros remain visible by default. A printer may
hide an individual public macro using its normal Klipper macro variable:

```ini
[gcode_macro MAINTENANCE]
variable_portal_visible: False
gcode:
  M105
```

Moonraker exposes that configured `variable_portal_visible` as runtime
`portal_visible`. The portal reads it with `printer.objects.query`; bool and
numeric 0/1 are supported. `SET_GCODE_VARIABLE ... VARIABLE=portal_visible ...`
is reflected on the next macro-list refresh. Missing/unsupported markers keep
public macros visible; no printer configuration is written by the client.

## Metadata and bounded images

Details prefer a validated 300x300 PNG, fall back to 100x100 if it is absent or
fails validation/transfer, and ignore 48x48. Actual PNG width/height are exported;
metadata declarations must match the PNG. The encoded image cap is 128 KiB;
accepted dimensions stay inside 320x320 (409600 bytes at RGBA decode). Encoded
bytes are explicitly allocated in PSRAM, including small images. Invalid or
unavailable images leave a valid textual metadata result without image bytes.
The UI must still reject decoder errors and own its decoder/decoded memory.

The G-code list fetches names only and never requests per-file metadata or
thumbnails. Selected-file metadata and the separately pinned current/last-job
metadata retain their bounded 300/100 PNG handling. Metadata readers capture an
immutable shared owner under the backend lock, then copy PNG bytes outside it.
Selected-file queries have priority over optional discovery, list refreshes and
pinned-job metadata. A changed selection cancels stale metadata transfers at the
next HTTP I/O boundary; background job metadata yields to a pending selection.
HTTP parsing/deallocation and PNG copying run outside the shared state lock.
The independent WebSocket worker continues publishing printer state during HTTP
queries. Existing connection/read timeouts still bound an in-progress I/O call;
cancellation does not synchronously interrupt that call.

`filament_colors`, `filament_weights` and `referenced_tools` are optional arrays.
Tool records preserve array indices; invalid/missing color and weight fields
remain explicitly absent. Weight zero is a valid value. Color syntax is
`#RRGGBB`; at most ten tool records are published. Only indices explicitly present in a valid referenced array are marked
referenced. A known zero without an explicit reference can be hidden; an
unknown weight remains distinct from zero. A lone tool with missing per-tool
weight uses the aggregate weight when available. A missing total weight is
derived only from an entirely valid weight array, never by treating missing
entries as zero.

## Job history and reset

`klipper_client_last_job` returns a separate terminal summary, with metadata
copied using `klipper_client_last_job_metadata`. Selection changes and
`print_stats` clearing do not erase it. Metadata arriving after completion
increments the last-job revision. A new endpoint clears the old host's record.
The snapshot job token changes on a new observed job or reconnect baseline.

`K_ACTION_RESET_JOB` accepts the confirmed decimal `job_id` as its value and the
confirmed endpoint generation. It sends `SDCARD_RESET_FILE` only for the same
currently terminal job. Guards run at enqueue, dequeue, before HTTP open and
immediately before body write. A new printing/paused state rejects the reset.
This protects against locally observed changes; Moonraker provides no atomic
job-token conditional reset, so remote changes concurrent with transmission
cannot be made transactional. An uncertain POST remains UNKNOWN and is never
retried automatically.

The `tests/klipper_*regression.cpp` assertions compile against the production
policy/helpers with synthetic clocks. `tests/klipper_simulator.py --self-test`
checks the loopback fixture, including PNG sizes and optional metadata values;
it does not itself execute the ESP network backend or replace hardware tests.
