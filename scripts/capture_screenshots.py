#!/usr/bin/env python3
"""Capture store-listing screenshots from the Pebble emulator.

Drives the emulator only — no watchface C changes. Each scene applies settings
via app messages (Clay message keys), sets emulated time/battery, then grabs a
screenshot.

Scene file layering (later wins):
  defaults.location + defaults.* toggles  →  scene.settings overrides

Usage (from project root, with .venv activated):
	python3 scripts/capture_screenshots.py
	python3 scripts/capture_screenshots.py --scene hero timeline
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SCENES_PATH = Path(__file__).resolve().parent / "screenshot_scenes.json"
MESSAGE_KEYS_PATH = ROOT / "build" / "js" / "message_keys.json"
PBW_PATH = ROOT / "build" / f"{ROOT.name}.pbw"
SCREENSHOTS_DIR = ROOT / "screenshots"

SETTLE_SEC = 1.5
TIME_SETTLE_SEC = 0.75


def find_pebble() -> str:
	venv_pebble = ROOT.parent / ".venv" / "bin" / "pebble"
	if venv_pebble.is_file():
		return str(venv_pebble)
	which = shutil.which("pebble")
	if which:
		return which
	raise SystemExit("pebble CLI not found — activate ../.venv or install pebble-tool")


def load_message_keys() -> dict[str, int]:
	if not MESSAGE_KEYS_PATH.is_file():
		raise SystemExit(
			f"Missing {MESSAGE_KEYS_PATH.relative_to(ROOT)} — run `pebble build` first"
		)
	with MESSAGE_KEYS_PATH.open(encoding="utf-8") as fh:
		return json.load(fh)


def load_scenes(path: Path) -> dict:
	with path.open(encoding="utf-8") as fh:
		return json.load(fh)


def run_pebble(
	pebble: str,
	args: list[str],
	*,
	cwd: Path = ROOT,
	check: bool = True,
) -> subprocess.CompletedProcess:
	cmd = [pebble, *args]
	print("+", " ".join(cmd))
	return subprocess.run(cmd, cwd=cwd, check=check)


def ensure_build(pebble: str, skip_build: bool) -> None:
	if skip_build and PBW_PATH.is_file():
		return
	if PBW_PATH.is_file() and skip_build:
		return
	if not PBW_PATH.is_file():
		print("PBW missing — building...")
	run_pebble(pebble, ["build"])
	if not PBW_PATH.is_file():
		raise SystemExit(f"Build finished but PBW not found at {PBW_PATH}")


def wipe_emulator_persist(platform: str, sdk: str | None) -> None:
	try:
		from pebble_tool.sdk import get_sdk_persist_dir, sdk_manager
	except ImportError:
		print("Note: could not import pebble_tool — skip persist wipe")
		return

	target_sdk = sdk or sdk_manager.get_current_sdk()
	persist_dir = get_sdk_persist_dir(platform, target_sdk)
	if os.path.isdir(persist_dir):
		shutil.rmtree(persist_dir)
		print(f"Wiped emulator persist: {persist_dir}")


def install_once(pebble: str, platform: str) -> None:
	run_pebble(
		pebble,
		["install", "--emulator", platform, str(PBW_PATH)],
	)


META_DEFAULT_KEYS = frozenset({"time", "battery_percent", "location", "settings"})


def default_settings_dict(defaults: dict, message_keys: dict[str, int]) -> dict[str, int]:
	"""Settings from defaults — top-level keys and optional defaults.settings."""
	out: dict[str, int] = {}

	for name, value in defaults.items():
		if name in META_DEFAULT_KEYS:
			continue
		if name not in message_keys:
			raise SystemExit(f"Unknown default setting {name!r} in scenes file")
		out[name] = int(value)

	for name, value in defaults.get("settings", {}).items():
		if name not in message_keys:
			raise SystemExit(f"Unknown default setting {name!r} in defaults.settings")
		out[name] = int(value)

	return out


def merge_settings(
	defaults: dict,
	scene: dict,
	message_keys: dict[str, int],
) -> dict[str, int]:
	"""Merge location + default toggles + per-scene overrides (scene wins)."""
	out: dict[str, int] = {}

	loc = defaults.get("location", {})
	for name, value in loc.items():
		if name not in message_keys:
			raise SystemExit(f"Unknown location setting {name!r}")
		out[name] = int(value)

	out.update(default_settings_dict(defaults, message_keys))

	scene_settings = scene.get("settings", {})
	for name, value in scene_settings.items():
		if name not in message_keys:
			raise SystemExit(f"Unknown setting {name!r} in scene {scene.get('id')!r}")
		out[name] = int(value)

	return out


def send_settings(pebble: str, platform: str, settings: dict[str, int], message_keys: dict[str, int]) -> None:
	int_args: list[str] = []
	for name, value in sorted(settings.items(), key=lambda item: message_keys[item[0]]):
		key = message_keys[name]
		int_args.append(f"{key}={value}")
	if not int_args:
		return
	run_pebble(
		pebble,
		["send-app-message", "--emulator", platform, "--int", *int_args],
	)


def set_emulator_time(pebble: str, platform: str, time_str: str) -> None:
	"""Set local emulated time; send twice so the face repaints."""
	run_pebble(pebble, ["emu-set-time", "--emulator", platform, time_str])
	time.sleep(TIME_SETTLE_SEC)
	run_pebble(pebble, ["emu-set-time", "--emulator", platform, time_str])
	time.sleep(TIME_SETTLE_SEC)


def set_emulator_battery(pebble: str, platform: str, percent: int) -> None:
	run_pebble(
		pebble,
		["emu-battery", "--emulator", platform, "--percent", str(percent)],
	)


def capture_screenshot(
	pebble: str,
	platform: str,
	out_path: Path,
) -> None:
	out_path.parent.mkdir(parents=True, exist_ok=True)
	run_pebble(
		pebble,
		[
			"screenshot",
			"--emulator",
			platform,
			"--no-open",
			"--no-correction",
			str(out_path),
		],
	)


def capture_scene(
	pebble: str,
	platform: str,
	scene: dict,
	defaults: dict,
	message_keys: dict[str, int],
	out_dir: Path,
) -> Path:
	scene_id = scene["id"]
	filename = scene.get("filename") or f"{platform}_{scene_id}.png"
	out_path = out_dir / filename
	time_str = scene.get("time") or defaults.get("time", "10:10:00")
	battery = scene.get("battery_percent", defaults.get("battery_percent", 85))

	print(f"\n=== Scene: {scene_id} — {scene.get('description', '')} ===", flush=True)

	set_emulator_battery(pebble, platform, int(battery))

	settings = merge_settings(defaults, scene, message_keys)
	send_settings(pebble, platform, settings, message_keys)
	time.sleep(SETTLE_SEC)
	# PKJS may push Clay localStorage shortly after install — send again before capture.
	send_settings(pebble, platform, settings, message_keys)
	time.sleep(0.5)

	# Set time last — PKJS/settings apply can lag; time must match the frame.
	set_emulator_time(pebble, platform, time_str)

	capture_screenshot(pebble, platform, out_path)
	return out_path


def publish_hero(pebble: str, args: argparse.Namespace) -> None:
	"""Delegate to pebble screenshot --all-platforms (publish-compatible naming)."""
	extra = []
	if args.sdk:
		extra.extend(["--sdk", args.sdk])
	run_pebble(
		pebble,
		["screenshot", "--all-platforms", "--no-open", *extra],
	)


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument(
		"--scenes-file",
		type=Path,
		default=SCENES_PATH,
		help="JSON file with scene definitions",
	)
	parser.add_argument(
		"--scene",
		nargs="*",
		help="Scene id(s) to capture (default: all)",
	)
	parser.add_argument(
		"--platform",
		default="emery",
		help="Emulator platform (default: emery)",
	)
	parser.add_argument(
		"--out-dir",
		type=Path,
		default=SCREENSHOTS_DIR,
		help="Output directory",
	)
	parser.add_argument("--skip-build", action="store_true")
	parser.add_argument("--skip-install", action="store_true")
	parser.add_argument(
		"--no-fresh",
		action="store_true",
		help="Keep emulator persist storage (default: wipe before install)",
	)
	parser.add_argument(
		"--publish-hero",
		action="store_true",
		help="Capture single publish-style screenshot via `pebble screenshot --all-platforms`",
	)
	parser.add_argument("--sdk", help="SDK version for emulator")
	return parser.parse_args()


def main() -> None:
	args = parse_args()
	pebble = find_pebble()
	message_keys = load_message_keys()

	if args.publish_hero:
		ensure_build(pebble, args.skip_build)
		publish_hero(pebble, args)
		return

	config = load_scenes(args.scenes_file)
	defaults = config.get("defaults", {})
	all_scenes = config.get("scenes", [])
	if not all_scenes:
		raise SystemExit("No scenes defined")

	if args.scene:
		want = set(args.scene)
		scenes = [s for s in all_scenes if s["id"] in want]
		missing = want - {s["id"] for s in scenes}
		if missing:
			raise SystemExit(f"Unknown scene(s): {', '.join(sorted(missing))}")
	else:
		scenes = all_scenes

	ensure_build(pebble, args.skip_build)
	if not args.skip_install:
		if not args.no_fresh:
			wipe_emulator_persist(args.platform, args.sdk)
		install_once(pebble, args.platform)
		time.sleep(3)

	captured: list[Path] = []
	for scene in scenes:
		path = capture_scene(
			pebble,
			args.platform,
			scene,
			defaults,
			message_keys,
			args.out_dir,
		)
		captured.append(path)

	print("\nCaptured:")
	for path in captured:
		print(f"  {path}")


if __name__ == "__main__":
	main()
