"""Compare resolved LVGL control colors with the original UI; no hardware IO."""
import argparse
import hashlib
import json
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CASES = [(f"view_{view:02}", view, []) for view in range(11)] + [
    ("file_selected", 5, ["portal_turret.gcode"]),
    ("macro_selected", 6, ["START_PRINT"]),
    ("object_selected", 7, ["cube_2"]),
    ("material_dropdown", 4, ["@opendropdown:0"]),
    ("step_dropdown", 4, ["@opendropdown:1"]),
    ("sleep_dropdown", 8, ["@opendropdown:0"]),
    ("sound_off", 8, ["@switch:0:0", "@tick"]),
    ("confirm_cancel", 2, ["CANCELAR"]),
    ("confirm_emergency", 1, ["PARADA"]),
    ("target_keypad", 4, ["210 °C"]),
    ("tuning", 2, ["AJUSTES DE IMPRESIÓN"]),
    ("wifi", 8, ["WI-FI Y MOONRAKER"]),
    ("wifi_keyboard", 8, ["WI-FI Y MOONRAKER", "@focus:0"]),
    ("wifi_password", 8, ["WI-FI Y MOONRAKER", "@focus:1"]),
    ("moonraker", 8, ["WI-FI Y MOONRAKER", "Moonraker"]),
    ("moonraker_keyboard", 8, ["WI-FI Y MOONRAKER", "Moonraker", "@focus:0"]),
    ("wifi_dropdown", 8, ["WI-FI Y MOONRAKER", "@opendropdown:0"]),
]


def run(exe, out, view, actions):
    encoded = [a if a.startswith("@") else "hex:" + a.encode().hex() for a in actions]
    result = subprocess.run([str(exe.resolve()), str(out.resolve()), str(view), *encoded],
                            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=45)
    if result.returncode:
        raise RuntimeError(result.stdout[-4500:] + result.stderr)
    return result.stdout


def palette(records):
    """Keep all widget states/parts; compare painted scrollbar parts by style.

    LVGL labels/images also carry SCROLLABLE by default, but have no scrollbar
    paint. Their inherited text color and count change when the header labels
    are removed, and do not describe any scrollbar track or thumb.
    """
    controls = []
    scrollbars = []
    for line in records:
        fields = line.split()
        if fields[0] != "STYLE":
            continue
        if int(fields[1]) < 10000:
            controls.append(line)
        elif int(fields[6]) or (int(fields[8]) and int(fields[9])) or int(fields[15]):
            # No text is painted by a scrollbar; ignore its inherited text color.
            scrollbars.append(" ".join(fields[4:10] + fields[11:]))
    canonical = controls + [f"SCROLLBAR {count} {style}" for style, count in sorted(Counter(scrollbars).items())]
    return {"sha256": hashlib.sha256("\n".join(canonical).encode()).hexdigest(),
            "control_records": len(controls), "painted_scrollbars": len(scrollbars)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / "tests/portal_host/output")
    parser.add_argument("--baseline", type=Path, default=ROOT / "tests/fixtures/portal-control-palette-0.6.5.json")
    parser.add_argument("--record-baseline", action="store_true")
    args = parser.parse_args()
    exe = args.host / "portal_host.exe"
    fingerprints = json.loads((args.host / "build_sources.json").read_text())
    if any(hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest for name, digest in fingerprints.items()):
        raise SystemExit("Host sources changed; rebuild before capturing or comparing control styles")
    results = {}
    style_output = args.host / "styles"
    style_output.mkdir(parents=True, exist_ok=True)
    for name, view, actions in CASES:
        output = run(exe, style_output, view, actions + ["@styles"])
        records = [line for line in output.splitlines() if line.startswith(("STYLE ", "STYLES "))]
        assert len(records) > 1, name
        results[name] = palette(records)
        print(f"STYLES {name}: {len(records)-1} resolved part/state records")
    if args.record_baseline:
        args.baseline.parent.mkdir(parents=True, exist_ok=True)
        args.baseline.write_text(json.dumps({"source_sha256": fingerprints, "cases": results}, indent=2) + "\n")
        print(f"Recorded original control palette: {args.baseline}")
        return
    before = json.loads(args.baseline.read_text())["cases"]
    differences = {name: {"before": before.get(name), "after": records}
                   for name, records in results.items() if before.get(name) != records}
    report = {"cases": len(results), "passed": len(results)-len(differences),
              "resolved_style_records": sum(p["control_records"] for p in results.values()),
              "differences": differences, "source_sha256": fingerprints,
              "regression_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), "hardware": False}
    (args.host / "visual_style_report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"Original control colors unchanged: {report['passed']}/{report['cases']} cases")
    raise SystemExit(bool(differences))


if __name__ == "__main__":
    main()
