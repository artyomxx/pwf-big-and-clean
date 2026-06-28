#!/usr/bin/env python3
"""Analyze Pebble health export logs and simulate timeline wake detection.

Usage:
  python3 scripts/analyze_wake.py health_export.log
  pebble logs | python3 scripts/analyze_wake.py -
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Iterable

# Defaults match timeline_wake.h (overridden by [health] cfg line when present)
MIN_MERGED_SLEEP_SEC = 3 * 3600
SLEEP_MERGE_GAP_SEC = 30 * 60
SLEEP_LOOKBACK_SEC = 24 * 3600
ACTIVITY_AFTER_BED_SEC = 2 * 3600
MIN_SLEEP_SEGMENT_SEC = 40 * 60
MIN_STEP_REST_QUIET_SEC = 6 * 3600
STEP_RESUME_LOOKBACK_SEC = 30 * 3600
MORNING_WAKE_HOUR_START = 5
MORNING_WAKE_HOUR_END = 12
WAKE_CONFIRM_MINUTES = 2
ON_WRIST_LOOKBACK_MIN = 10
AWAKE_RECENT_SLEEP_SEC = 10 * 60
AWAKE_GRACE_SLEEP_SEC = 30 * 60
AWAKE_GRACE_MAX_STEPS = 20
SECONDS_PER_DAY = 24 * 3600
DEFAULT_AWAKE_MINUTES = 16 * 60
DEFAULT_MANUAL_WAKE_MIN = 7 * 60


@dataclass
class SleepSegment:
	start: int
	end: int
	activity: int
	duration: int


@dataclass
class Minute:
	utc: int
	steps: int
	hr: int
	vmc: int
	invalid: bool


@dataclass
class ExportMeta:
	now: int
	tz_min: int
	scan_start: int
	day_start: int


@dataclass
class ExportCfg:
	merged_sleep_h: int
	merge_gap_min: int
	lookback_h: int
	after_bed_h: int
	confirm_min: int


def parse_log(
	lines: Iterable[str],
) -> tuple[ExportMeta | None, ExportCfg | None, list[SleepSegment], list[Minute], list[tuple[int, int]]]:
	meta: ExportMeta | None = None
	cfg: ExportCfg | None = None
	sleep: list[SleepSegment] = []
	minutes: list[Minute] = []
	gaps: list[tuple[int, int]] = []

	meta_re = re.compile(r"\[health\] meta now=(\d+) tz=(-?\d+) scan=(\d+) day=(\d+)")
	cfg_re = re.compile(
		r"\[health\] cfg merged_sleep_h=(\d+) merge_gap_min=(\d+) "
		r"lookback_h=(\d+) after_bed_h=(\d+) confirm_min=(\d+)"
	)
	cfg_mid_re = re.compile(
		r"\[health\] cfg merged_sleep_h=(\d+) merge_gap_min=(\d+) off_wrist_min=\d+ confirm_min=(\d+)"
	)
	cfg_legacy_re = re.compile(
		r"\[health\] cfg off_wrist_min=\d+ stale_sleep_min=\d+ confirm_min=(\d+)"
	)
	sleep_re = re.compile(r"\[health\] sleep (\d+) (\d+) (-?\d+) (-?\d+)")
	min_re = re.compile(r"\[health\] min (\d+)((?: \d+,\d+,\d+,\d+)+)")
	gap_re = re.compile(r"\[health\] gap (\d+) (\d+)")

	for line in lines:
		if m := meta_re.search(line):
			meta = ExportMeta(
				now=int(m.group(1)),
				tz_min=int(m.group(2)),
				scan_start=int(m.group(3)),
				day_start=int(m.group(4)),
			)
			continue
		if m := cfg_re.search(line):
			cfg = ExportCfg(
				merged_sleep_h=int(m.group(1)),
				merge_gap_min=int(m.group(2)),
				lookback_h=int(m.group(3)),
				after_bed_h=int(m.group(4)),
				confirm_min=int(m.group(5)),
			)
			continue
		if m := cfg_mid_re.search(line):
			cfg = ExportCfg(
				merged_sleep_h=int(m.group(1)),
				merge_gap_min=int(m.group(2)),
				lookback_h=SLEEP_LOOKBACK_SEC // 3600,
				after_bed_h=ACTIVITY_AFTER_BED_SEC // 3600,
				confirm_min=int(m.group(3)),
			)
			continue
		if m := cfg_legacy_re.search(line):
			cfg = ExportCfg(
				merged_sleep_h=MIN_MERGED_SLEEP_SEC // 3600,
				merge_gap_min=SLEEP_MERGE_GAP_SEC // 60,
				lookback_h=SLEEP_LOOKBACK_SEC // 3600,
				after_bed_h=ACTIVITY_AFTER_BED_SEC // 3600,
				confirm_min=int(m.group(1)),
			)
			continue
		if m := sleep_re.search(line):
			sleep.append(SleepSegment(
				start=int(m.group(1)),
				end=int(m.group(2)),
				activity=int(m.group(3)),
				duration=int(m.group(4)),
			))
			continue
		if m := gap_re.search(line):
			gaps.append((int(m.group(1)), int(m.group(2))))
			continue
		if m := min_re.search(line):
			start = int(m.group(1))
			for i, chunk in enumerate(m.group(2).strip().split()):
				parts = [int(x) for x in chunk.split(",")]
				if len(parts) != 4:
					continue
				minutes.append(Minute(
					utc=start + i * 60,
					steps=parts[0],
					hr=parts[1],
					vmc=parts[2],
					invalid=parts[3] == 1,
				))

	return meta, cfg, sleep, minutes, gaps


def apply_cfg(cfg: ExportCfg | None) -> None:
	global MIN_MERGED_SLEEP_SEC, SLEEP_MERGE_GAP_SEC, SLEEP_LOOKBACK_SEC
	global ACTIVITY_AFTER_BED_SEC, WAKE_CONFIRM_MINUTES
	if cfg is None:
		return
	MIN_MERGED_SLEEP_SEC = cfg.merged_sleep_h * 3600
	SLEEP_MERGE_GAP_SEC = cfg.merge_gap_min * 60
	SLEEP_LOOKBACK_SEC = cfg.lookback_h * 3600
	ACTIVITY_AFTER_BED_SEC = cfg.after_bed_h * 3600
	WAKE_CONFIRM_MINUTES = cfg.confirm_min


def fmt_local(utc: int, tz_min: int) -> str:
	local = datetime.fromtimestamp(utc, tz=timezone.utc)
	adjusted = local.timestamp() - tz_min * 60
	return datetime.fromtimestamp(adjusted, tz=timezone.utc).strftime("%Y-%m-%d %H:%M")


def align_minute_utc(utc: int) -> int:
	return utc - (utc % 60)


def manual_wake_utc_for_now(meta: ExportMeta, awake_minutes: int = DEFAULT_AWAKE_MINUTES) -> int:
	now = meta.now
	wake_utc = meta.day_start + DEFAULT_MANUAL_WAKE_MIN * 60
	if wake_utc > now + 12 * 3600:
		wake_utc -= SECONDS_PER_DAY
	elif wake_utc < now - awake_minutes * 60:
		wake_utc += SECONDS_PER_DAY
	return wake_utc


def merge_sleep_segments(segments: list[SleepSegment]) -> list[SleepSegment]:
	if not segments:
		return []
	ordered = sorted(segments, key=lambda s: s.start)
	merged: list[SleepSegment] = []
	for seg in ordered:
		if not merged:
			merged.append(SleepSegment(seg.start, seg.end, seg.activity, seg.end - seg.start))
			continue
		last = merged[-1]
		if seg.start <= last.end + SLEEP_MERGE_GAP_SEC:
			last.end = max(last.end, seg.end)
			last.start = min(last.start, seg.start)
			last.duration = last.end - last.start
		else:
			merged.append(SleepSegment(seg.start, seg.end, seg.activity, seg.end - seg.start))
	return merged


def in_sleep_segment_at(segments: list[SleepSegment], utc: int) -> bool:
	return any(seg.start <= utc < seg.end for seg in segments)


def find_last_sleep_end(segments: list[SleepSegment], now: int) -> int | None:
	last_end = 0
	search_start = now - SLEEP_LOOKBACK_SEC
	for seg in segments:
		if seg.end <= now and seg.end > last_end and seg.start >= search_start:
			last_end = seg.end
	return last_end or None


def find_merged_sleep_session(
	segments: list[SleepSegment],
	now: int,
) -> tuple[int, int] | None:
	if in_sleep_segment_at(segments, now):
		return None

	search_start = now - SLEEP_LOOKBACK_SEC
	window = [s for s in segments if s.end > search_start and s.start < now]
	if not window:
		return None

	merged = merge_sleep_segments(window)
	last_idx = -1
	for i, seg in enumerate(merged):
		if seg.end <= now and (last_idx < 0 or seg.end > merged[last_idx].end):
			last_idx = i

	if last_idx < 0:
		return None

	merged_start = merged[last_idx].start
	merged_end = merged[last_idx].end
	idx = last_idx

	while idx > 0:
		prev = merged[idx - 1]
		cur = merged[idx]
		if merged_end - merged_start >= MIN_MERGED_SLEEP_SEC:
			break
		if prev.start < search_start:
			break
		if prev.end + SLEEP_MERGE_GAP_SEC < cur.start:
			break
		merged_start = prev.start
		idx -= 1

	if merged_end - merged_start < MIN_MERGED_SLEEP_SEC:
		return None

	return merged_start, merged_end


def find_longest_sleep_segment(
	segments: list[SleepSegment],
	now: int,
) -> tuple[int, int] | None:
	if in_sleep_segment_at(segments, now):
		return None

	search_start = now - SLEEP_LOOKBACK_SEC
	best: SleepSegment | None = None

	for seg in segments:
		if seg.start < search_start or seg.end > now:
			continue
		if seg.duration < MIN_SLEEP_SEGMENT_SEC:
			continue
		if best is None or seg.duration > best.duration:
			best = seg
		elif seg.duration == best.duration and seg.end > best.end:
			best = seg

	if best is None:
		return None

	return best.start, best.end


def resolve_sleep_wake(
	segments: list[SleepSegment],
	now: int,
) -> tuple[int, int] | None:
	session = find_merged_sleep_session(segments, now)
	if session is not None:
		return session
	return find_longest_sleep_segment(segments, now)


def minute_has_steps(m: Minute) -> bool:
	return not m.invalid and m.steps > 0


def local_hour(utc: int, tz_min: int) -> int:
	local = datetime.fromtimestamp(utc, tz=timezone.utc)
	adjusted = local.timestamp() - tz_min * 60
	return datetime.fromtimestamp(adjusted, tz=timezone.utc).hour


def wake_from_step_quiet(
	timeline: list[tuple[int, Minute | None]],
	now: int,
	tz_min: int,
) -> int | None:
	last_streak_end = now - SLEEP_LOOKBACK_SEC
	streak = 0
	streak_start = 0
	best_quiet = 0
	best_wake = 0
	best_morning = False

	for minute_utc, minute in timeline:
		if minute_utc >= now:
			break
		if minute is not None and minute_has_steps(minute):
			if streak > 0 and minute_utc == streak_start + streak * 60:
				streak += 1
			else:
				streak = 1
				streak_start = minute_utc
			if streak >= WAKE_CONFIRM_MINUTES:
				quiet = streak_start - last_streak_end
				morning = MORNING_WAKE_HOUR_START <= local_hour(streak_start, tz_min) < MORNING_WAKE_HOUR_END
				better = quiet > best_quiet
				if quiet == best_quiet and morning and not best_morning:
					better = True
				if quiet == best_quiet and morning == best_morning and streak_start > best_wake:
					better = True
				if quiet >= MIN_STEP_REST_QUIET_SEC and better:
					best_quiet = quiet
					best_wake = streak_start
					best_morning = morning
		else:
			if streak >= WAKE_CONFIRM_MINUTES:
				last_streak_end = streak_start + streak * 60
			streak = 0

	if best_wake <= 0 or best_wake >= now:
		return None
	return best_wake


def wake_from_step_resume(
	timeline: list[tuple[int, Minute | None]],
	now: int,
) -> int | None:
	last_step_minute = 0
	best_wake = 0
	scan_start = now - STEP_RESUME_LOOKBACK_SEC

	for minute_utc, minute in timeline:
		if minute_utc < scan_start or minute_utc >= now:
			continue
		if minute is not None and minute_has_steps(minute):
			if last_step_minute > 0:
				quiet = minute_utc - (last_step_minute + 60)
				if quiet >= MIN_STEP_REST_QUIET_SEC:
					best_wake = minute_utc
			last_step_minute = minute_utc

	if best_wake <= 0 or best_wake >= now:
		return None
	return best_wake


def minute_has_activity(m: Minute) -> bool:
	return not m.invalid and (m.steps > 0 or m.hr > 0)


def timeline_bed_utc(wake_utc: int, awake_minutes: int = DEFAULT_AWAKE_MINUTES) -> int:
	return wake_utc + awake_minutes * 60


def timeline_activity_floor_utc(
	meta: ExportMeta,
	cached_wake: int | None,
	segments: list[SleepSegment] | None = None,
	awake_minutes: int = DEFAULT_AWAKE_MINUTES,
) -> int:
	if cached_wake:
		floor = timeline_bed_utc(cached_wake, awake_minutes) + ACTIVITY_AFTER_BED_SEC
	else:
		manual_wake = manual_wake_utc_for_now(meta, awake_minutes)
		floor = timeline_bed_utc(manual_wake, awake_minutes) + ACTIVITY_AFTER_BED_SEC

	if segments is not None:
		session = find_longest_sleep_segment(segments, meta.now)
		if session is not None:
			after_sleep = session[1] + AWAKE_RECENT_SLEEP_SEC
			if after_sleep > floor:
				floor = after_sleep

	return floor


def wake_from_activity_after_bed(
	timeline: list[tuple[int, Minute | None]],
	now: int,
	floor: int,
) -> int | None:
	if floor >= now:
		return None

	streak = 0
	streak_start = 0
	for minute_utc, minute in timeline:
		if minute_utc < floor or minute_utc >= now:
			continue
		if minute is not None and minute_has_steps(minute):
			if streak > 0 and minute_utc == streak_start + streak * 60:
				streak += 1
			else:
				streak = 1
				streak_start = minute_utc
			if streak >= WAKE_CONFIRM_MINUTES:
				return streak_start
		else:
			streak = 0

	return None


def expand_minutes(minutes: list[Minute], gaps: list[tuple[int, int]], scan_start: int, now: int) -> list[tuple[int, Minute | None]]:
	by_utc = {align_minute_utc(m.utc): m for m in minutes}
	timeline: list[tuple[int, Minute | None]] = []
	cursor = align_minute_utc(scan_start)
	now_aligned = align_minute_utc(now)
	while cursor < now_aligned:
		if cursor in by_utc:
			timeline.append((cursor, by_utc[cursor]))
		else:
			in_gap = any(start <= cursor < end for start, end in gaps)
			timeline.append((cursor, None if in_gap else Minute(cursor, 0, 0, 0, True)))
		cursor += 60
	return timeline


def recent_activity_in_last_minutes(minutes: list[Minute], now: int, lookback_min: int) -> bool:
	start = now - lookback_min * 60
	return any(m.utc >= start and m.utc < now and minute_has_activity(m) for m in minutes)


def recent_data_present(minutes: list[Minute], now: int, lookback_min: int) -> bool:
	start = now - lookback_min * 60
	return any(m.utc >= start and m.utc < now and not m.invalid for m in minutes)


def steps_since(minutes: list[Minute], since_utc: int, now: int) -> int:
	total = 0
	for m in minutes:
		if since_utc <= m.utc < now and not m.invalid:
			total += m.steps
	return total


def is_user_awake(segments: list[SleepSegment], minutes: list[Minute], now: int) -> bool:
	if recent_activity_in_last_minutes(minutes, now, ON_WRIST_LOOKBACK_MIN):
		return True
	if in_sleep_segment_at(segments, now):
		return False
	last_sleep_end = find_last_sleep_end(segments, now)
	if last_sleep_end is None:
		return True
	since = now - last_sleep_end
	if since < AWAKE_RECENT_SLEEP_SEC:
		return False
	if since < AWAKE_GRACE_SLEEP_SEC:
		if steps_since(minutes, last_sleep_end, now) < AWAKE_GRACE_MAX_STEPS:
			return False
	return True


def is_watch_on_wrist(minutes: list[Minute], now: int) -> bool:
	return recent_activity_in_last_minutes(minutes, now, ON_WRIST_LOOKBACK_MIN) \
		or recent_data_present(minutes, now, ON_WRIST_LOOKBACK_MIN)


def get_wakeup_time(
	meta: ExportMeta,
	segments: list[SleepSegment],
	timeline: list[tuple[int, Minute | None]],
	cached_wake: int | None,
	cached_last_sleep: int | None,
	wake_from_sleep: bool = True,
) -> tuple[str, int | None]:
	if not wake_from_sleep:
		return "manual (setting)", manual_wake_utc_for_now(meta)

	if in_sleep_segment_at(segments, meta.now):
		if cached_wake:
			return "cached (asleep now)", cached_wake
		return "manual (asleep now)", manual_wake_utc_for_now(meta)

	last_sleep = find_last_sleep_end(segments, meta.now)
	merged = find_merged_sleep_session(segments, meta.now)
	session = resolve_sleep_wake(segments, meta.now)

	if session is not None and cached_wake and session[1] != cached_wake:
		if abs(session[1] - cached_wake) >= AWAKE_RECENT_SLEEP_SEC:
			return "sleep (overrides stale cache)", session[1]

	if last_sleep and cached_last_sleep == last_sleep and cached_wake:
		if meta.now < timeline_bed_utc(cached_wake) + ACTIVITY_AFTER_BED_SEC:
			if not in_sleep_segment_at(segments, meta.now):
				return "cached (same last_sleep)", cached_wake

	if cached_wake and meta.now < timeline_bed_utc(cached_wake) + ACTIVITY_AFTER_BED_SEC:
		if not in_sleep_segment_at(segments, meta.now):
			return "cached (before bed+grace)", cached_wake

	if session is not None:
		if merged is not None:
			return "merged sleep", session[1]
		return "longest sleep segment", session[1]

	step_wake = wake_from_step_resume(timeline, meta.now)
	if step_wake is not None:
		return "step resume (put-on)", step_wake

	step_wake = wake_from_step_quiet(timeline, meta.now, meta.tz_min)
	if step_wake is not None:
		return "step quiet (no sleep API)", step_wake

	floor = timeline_activity_floor_utc(meta, cached_wake, segments)
	activity_wake = wake_from_activity_after_bed(timeline, meta.now, floor)
	if activity_wake is not None:
		return "activity after bed", activity_wake

	return "manual (no signal)", manual_wake_utc_for_now(meta)


def main() -> int:
	if len(sys.argv) != 2:
		print(__doc__)
		return 1

	path = sys.argv[1]
	if path == "-":
		lines = sys.stdin.readlines()
	else:
		with open(path, encoding="utf-8", errors="replace") as f:
			lines = f.readlines()

	meta, cfg, sleep, minutes, gaps = parse_log(lines)
	if meta is None:
		print("No [health] meta line found. Export first with DbgProfileCmd=3.")
		return 1

	apply_cfg(cfg)
	now = meta.now
	full_timeline = expand_minutes(minutes, gaps, meta.scan_start, now)

	last_sleep = find_last_sleep_end(sleep, now)
	merged = find_merged_sleep_session(sleep, now)
	longest = find_longest_sleep_segment(sleep, now)
	step_resume = wake_from_step_resume(full_timeline, now)
	step_wake = wake_from_step_quiet(full_timeline, now, meta.tz_min)
	floor = timeline_activity_floor_utc(meta, None, sleep)
	activity_wake = wake_from_activity_after_bed(full_timeline, now, floor)
	src, wake = get_wakeup_time(meta, sleep, full_timeline, None, None)
	awake = is_user_awake(sleep, minutes, now)
	on_wrist = is_watch_on_wrist(minutes, now)

	print(f"Export window: {fmt_local(meta.scan_start, meta.tz_min)} → {fmt_local(now, meta.tz_min)} (local)")
	if cfg:
		print(
			f"Firmware cfg: merged={cfg.merged_sleep_h}h gap={cfg.merge_gap_min}min "
			f"lookback={cfg.lookback_h}h after_bed={cfg.after_bed_h}h confirm={cfg.confirm_min}min"
		)
	print(f"Sleep segments (raw): {len(sleep)}")
	for seg in sorted(sleep, key=lambda s: s.end, reverse=True)[:8]:
		print(f"  {fmt_local(seg.start, meta.tz_min)} – {fmt_local(seg.end, meta.tz_min)}  ({seg.duration // 60} min)")
	print()

	if last_sleep:
		print(f"Last sleep end (API):  {fmt_local(last_sleep, meta.tz_min)}")
	else:
		print("Last sleep end (API):  none")

	if merged:
		start, end = merged
		dur = end - start
		print(
			f"Merged sleep session: {fmt_local(start, meta.tz_min)} – {fmt_local(end, meta.tz_min)} "
			f"({dur // 3600}h{(dur % 3600) // 60:02d}m)"
		)
	else:
		print("Merged sleep session: none (need ≥3h in last 24h, not asleep now)")

	if longest and longest != merged:
		start, end = longest
		dur = end - start
		print(
			f"Longest sleep segment: {fmt_local(start, meta.tz_min)} – {fmt_local(end, meta.tz_min)} "
			f"({dur // 3600}h{(dur % 3600) // 60:02d}m, ≥{MIN_SLEEP_SEGMENT_SEC // 60}min)"
		)
	elif longest:
		print(f"Longest sleep segment: same as merged session")

	if step_resume:
		print(f"Step-resume wake candidate: {fmt_local(step_resume, meta.tz_min)} (first steps after ≥{MIN_STEP_REST_QUIET_SEC // 3600}h quiet)")

	if step_wake:
		print(f"Step-quiet wake candidate: {fmt_local(step_wake, meta.tz_min)} (≥{MIN_STEP_REST_QUIET_SEC // 3600}h without steps)")

	print(f"Activity search floor: {fmt_local(floor, meta.tz_min)} (bedtime+{ACTIVITY_AFTER_BED_SEC // 3600}h)")
	if activity_wake:
		print(f"Activity wake candidate: {fmt_local(activity_wake, meta.tz_min)}")
	else:
		print("Activity wake candidate: none")

	print()
	print(f"At export time: on_wrist={on_wrist} awake={awake} would_run_logic={on_wrist and awake}")
	print(f"In sleep segment now: {in_sleep_segment_at(sleep, now)}")
	print()
	if wake:
		print(f"Firmware algorithm: {src} → {fmt_local(wake, meta.tz_min)}")
	else:
		print(f"Firmware algorithm: {src}")

	return 0


if __name__ == "__main__":
	raise SystemExit(main())
