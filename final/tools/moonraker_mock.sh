#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -n "${MOONRAKER_PYTHON:-}" ]]; then
    exec "$MOONRAKER_PYTHON" "$script_dir/moonraker_console.py" "$@"
fi
for candidate in /c/Espressif/tools/python/v5.5.6/venv/Scripts/python.exe \
                 /d/Espressif/tools/python/v5.5.6/venv/Scripts/python.exe python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 && "$candidate" --version >/dev/null 2>&1; then
        exec "$candidate" "$script_dir/moonraker_console.py" "$@"
    fi
done
printf '%s\n' 'No se encontro Python 3. Define MOONRAKER_PYTHON con la ruta a su ejecutable.' >&2
exit 1
