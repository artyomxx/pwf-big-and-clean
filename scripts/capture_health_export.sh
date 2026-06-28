#!/usr/bin/env bash
# Capture Pebble health export from watchface logs.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/health_export.log}"

if [[ -f "$ROOT/../.venv/bin/activate" ]]; then
	# shellcheck disable=SC1091
	source "$ROOT/../.venv/bin/activate"
fi

echo "Writing health export to: $OUT"
echo "Trigger export: pebble send-app-message --phone <ip> --int 10026=3"
echo "(or long-press SELECT if you wire that up)"
echo

: >"$OUT"
pebble logs --raw | tee -a "$OUT" | rg '\[health\]|profile: health export'

echo
echo "Analyze with:"
echo "  python3 $ROOT/scripts/analyze_wake.py $OUT"
