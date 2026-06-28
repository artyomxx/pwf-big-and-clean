#!/usr/bin/env bash
# Combine all PNGs in screenshots/ into one animated GIF.
# Uses palette optimization (--colors) at 2 fps by default.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

IN_DIR="$ROOT/screenshots"
OUT="$IN_DIR/screenshots.gif"
FPS=2
positional=()

usage() {
	echo "Usage: $(basename "$0") [--fps=N] [input_dir] [output.gif]" >&2
	exit 1
}

while [[ $# -gt 0 ]]; do
	case "$1" in
		--fps=*)
			FPS="${1#*=}"
			shift
			;;
		-h|--help)
			usage
			;;
		-*)
			echo "Unknown option: $1" >&2
			usage
			;;
		*)
			positional+=("$1")
			shift
			;;
	esac
done

if ((${#positional[@]} > 0)); then
	IN_DIR="${positional[0]}"
	OUT="${IN_DIR}/screenshots.gif"
fi
if ((${#positional[@]} > 1)); then
	OUT="${positional[1]}"
fi

if ! command -v ffmpeg >/dev/null 2>&1; then
	echo "ffmpeg not found — install ffmpeg first" >&2
	exit 1
fi

shopt -s nullglob
pngs=("$IN_DIR"/*.png)
if ((${#pngs[@]} == 0)); then
	echo "No PNG files in $IN_DIR" >&2
	exit 1
fi

IFS=$'\n'
pngs=($(printf '%s\n' "${pngs[@]}" | LC_ALL=C sort))
unset IFS

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/screenshots-gif.XXXXXX")
cleanup() { rm -rf "$tmpdir"; }
trap cleanup EXIT

i=0
for f in "${pngs[@]}"; do
	i=$((i + 1))
	printf -v name 'frame_%04d.png' "$i"
	cp "$f" "$tmpdir/$name"
done

mkdir -p "$(dirname "$OUT")"

echo "Combining ${#pngs[@]} PNGs at ${FPS} fps"
echo "  in:  $IN_DIR"
echo "  out: $OUT"

# --colors: stats_mode=single palettegen + paletteuse (from ffmpeg-gif.sh)
ffmpeg -y -hide_banner -loglevel warning \
	-framerate "$FPS" -f image2 -i "$tmpdir/frame_%04d.png" \
	-filter_complex "[0:v] split [a][b]; [a] palettegen=stats_mode=single [p]; [b][p] paletteuse=new=1" \
	"$OUT"

echo "Wrote $OUT"
