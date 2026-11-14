"""Compare exact widget geometry and protected styles with approved UI revisions.

The baseline is captured from the existing working tree before restyling.
All services are host mocks; no device or printer IO is possible.
"""
import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image
from portal_visual_regression import CASES, ROOT, palette, run

BASELINE = ROOT / "tests/fixtures/portal-pre-mosaic-2026-11-06.json"
DESIGN_DELTAS = (
    ROOT / "tests/fixtures/portal-powerpoint-2026-11-10.json",
    ROOT / "tests/fixtures/portal-powerpoint-final.json",
    ROOT / "tests/fixtures/portal-screensaver.json",
    ROOT / "tests/fixtures/portal-powerpoint-v3.json",
)


def digest(value):
    return hashlib.sha256(value).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, default=ROOT / "tests/portal_host/output")
    parser.add_argument("--record-baseline", action="store_true")
    parser.add_argument("--write-design-delta", type=Path, metavar="PATH",
                        help="Write the observed changes for a reviewed design revision")
    args = parser.parse_args()
    fingerprints = json.loads((args.host / "build_sources.json").read_text())
    if any(digest((ROOT / name).read_bytes()) != sha for name, sha in fingerprints.items()):
        raise SystemExit("Host sources changed; rebuild first")
    out = args.host / "mosaic_contract"
    out.mkdir(parents=True, exist_ok=True)
    results, details = {}, {}
    for name, view, actions in CASES:
        output = run(args.host / "portal_host.exe", out, view,
                     actions + ["@geometry", f"@capture:contract_{name}", "@protectedstyles"])
        geometry = [line for line in output.splitlines() if line.startswith("GEOMETRY ")]
        assert geometry, name
        styles = palette(line for line in output.splitlines() if line.startswith("STYLE "))
        raw = out / f"contract_{name}.rgb565"
        frame = Image.frombytes("RGB", (800, 480), raw.read_bytes(), "raw", "BGR;16")
        frame.save(raw.with_suffix(".png"))
        thermal = {}
        if name in ("view_01", "view_02", "view_03"):
            for y in (350, 408):
                # Rounded corners expose the newly styled surface behind the
                # card. Compare its opaque interior, including text and icons.
                thermal[str(y)] = digest(frame.crop((34, y+1, 322, y+51)).tobytes())
        if name == "view_04":
            cards = [(12, 134), (158, 134)]
            if args.record_baseline:
                cards.append((416, 52))
            for y, h in cards:
                thermal[str(y)] = digest(frame.crop((20, y+1, 300, y+h-1)).tobytes())
        text_pixels = {}
        for context, rect in (("view_05", (406, 422, 766, 452)), ("view_10", (389, 364, 761, 396))):
            if name == context:
                # Initial renderer frames retain the disabled state. The
                # synchronized action context enables Files' selected job.
                filename = "05_files" if view == 5 else "10_details"
                initial = Image.frombytes("RGB", (800, 480), (out / f"{filename}.rgb565").read_bytes(), "raw", "BGR;16")
                text_pixels["disabled_interior"] = digest(initial.crop(rect).tobytes())
        results[name] = {"geometry_sha256": digest("\n".join(geometry).encode()),
                         "widgets": len(geometry), "protected_styles": styles, "thermal_pixels": thermal,
                         "text_button_pixels": text_pixels}
        details[name] = geometry
    (out / "geometry_records.json").write_text(json.dumps(details, indent=2) + "\n")
    if args.record_baseline:
        BASELINE.write_text(json.dumps({"description": "Working tree before Portal mosaic restyling; all pre-existing edits retained",
                                       "source_sha256": fingerprints, "cases": results}, indent=2) + "\n")
        print(f"Recorded pre-mosaic baseline: {len(results)} contexts, {sum(r['widgets'] for r in results.values())} widget records")
        return
    before = json.loads(BASELINE.read_text())["cases"]
    # The lower material hint was removed. The two real thermal cards retain
    # their original pixel hashes.
    before["view_04"]["thermal_pixels"].pop("416")
    for delta in DESIGN_DELTAS:
        for name, changes in json.loads(delta.read_text(encoding="utf-8"))["cases"].items():
            before[name].update(changes)
    if args.write_design_delta:
        changes = {name: {key: value for key, value in result.items()
                          if before.get(name, {}).get(key) != value}
                   for name, result in results.items()}
        delta = {"description": "Approved Portal PowerPoint v3 UI revision",
                 "cases": {name: change for name, change in changes.items() if change}}
        args.write_design_delta.write_text(json.dumps(delta, indent=2) + "\n", encoding="utf-8")
        print(f"Recorded reviewed design delta: {len(delta['cases'])} contexts")
        return
    differences = {name: {"before": before.get(name), "after": result}
                   for name, result in results.items() if before.get(name) != result}
    report = {"contexts": len(results), "passed": len(results)-len(differences),
              "widget_records": sum(r["widgets"] for r in results.values()),
              "protected_style_records": sum(r["protected_styles"]["control_records"] for r in results.values()),
              "differences": differences, "source_sha256": fingerprints,
              "design_delta_sha256": {delta.name: digest(delta.read_bytes()) for delta in DESIGN_DELTAS},
              "regression_sha256": digest(Path(__file__).read_bytes()), "hardware": False}
    (args.host / "mosaic_contract_report.json").write_text(json.dumps(report, indent=2) + "\n")
    for name in differences:
        print(f"FAIL {name}: " + ", ".join(k for k in results[name] if before.get(name, {}).get(k) != results[name][k]))
    print(f"Geometry, protected controls and thermal pixels: {report['passed']}/{report['contexts']}; "
          f"{report['widget_records']} widgets; {report['protected_style_records']} resolved styles")
    raise SystemExit(bool(differences))


if __name__ == "__main__":
    main()
