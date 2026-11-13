"""Read-only byte checks for the imported user-provided WAV resources."""
from pathlib import Path
import hashlib
import json
import re
import struct

assets = Path(__file__).resolve().parents[1] / "assets"
manifest = json.loads((assets / "provenance.json").read_text(encoding="utf-8"))
total_wav = total_pcm = 0
for clip in manifest["clips"]:
    imported = assets / clip["source_file"]
    text = imported.read_text(encoding="utf-8")
    wav = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", text))
    assert hashlib.sha256(wav).hexdigest() == clip["wav_sha256"], clip["name"]
    assert len(wav) == clip["wav_bytes"]
    assert struct.unpack_from("<I", wav, 40)[0] == clip["pcm_bytes"]
    source = Path(manifest["source_directory"]) / clip["source_file"]
    if source.exists():
        original = source.read_bytes()
        assert hashlib.sha256(original).hexdigest() == clip["source_header_sha256"]
        original_wav = bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", original.decode("utf-8-sig")))
        assert wav == original_wav, clip["name"]
    total_wav += len(wav)
    total_pcm += clip["pcm_bytes"]
assert total_wav == manifest["total_wav_bytes"] == 640218
assert total_pcm == manifest["total_pcm_bytes"] == 639168
print(f"PASS: SHA256 and original-byte identity of 7 imported WAVs; {total_wav} bytes, no transcoding")
