#include "timeline_wake.h"
#include "debug_profile.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static TimelineSettings s_settings;
static TimelineWakeHooks s_hooks;
static int32_t s_tz_offset_min;
static time_t s_wake_utc;
static bool s_wake_valid;
static time_t s_last_sleep_start;
static time_t s_last_sleep_end;

typedef struct {
	time_t start;
	time_t end;
} SleepSegment;
static int32_t awake_minutes(void) {
	int32_t sleep_min = (s_settings.sleep_hours_x10 * 60) / 10;
	int32_t awake = (24 * 60) - sleep_min;
	return awake > 0 ? awake : 1;
}

static time_t local_midnight_utc(time_t now) {
	time_t local = now - (time_t) s_tz_offset_min * 60;
	struct tm *tm_ptr = gmtime(&local);
	if (!tm_ptr) {
		return now;
	}

	time_t local_midnight = local - (time_t) tm_ptr->tm_hour * 3600
		- (time_t) tm_ptr->tm_min * 60 - (time_t) tm_ptr->tm_sec;
	return local_midnight + (time_t) s_tz_offset_min * 60;
}

static time_t manual_wake_utc_for_now(void) {
	time_t now = time(NULL);
	time_t wake_utc = local_midnight_utc(now) + (time_t) s_settings.manual_wake_min * 60;

	if (wake_utc > now + 12 * 3600) {
		wake_utc -= SECONDS_PER_DAY;
	} else if (wake_utc < now - (time_t) awake_minutes() * 60) {
		wake_utc += SECONDS_PER_DAY;
	}

	return wake_utc;
}

typedef struct __attribute__((__packed__)) {
	uint8_t magic;
	time_t wake_utc;
	bool wake_valid;
	time_t last_sleep_start;
	time_t last_sleep_end;
} TimelineCyclePersist;

static bool timeline_persist_fits_cycle(const TimelineCyclePersist *p) {
	if (!p->wake_valid || p->wake_utc <= 0) {
		return false;
	}

	const time_t now = time(NULL);
	if (p->wake_utc > now + 5 * 60) {
		return false;
	}

	const time_t max_span = (time_t) awake_minutes() * 60 + TIMELINE_ACTIVITY_AFTER_BED_SEC;
	return (now - p->wake_utc) <= max_span;
}

static void timeline_wake_persist_clear(void) {
	persist_delete(PERSIST_KEY_TIMELINE_CYCLE);
}

static void timeline_wake_persist_save(void) {
	if (!s_wake_valid) {
		timeline_wake_persist_clear();
		return;
	}

	const TimelineCyclePersist p = {
		.magic = TIMELINE_PERSIST_MAGIC,
		.wake_utc = s_wake_utc,
		.wake_valid = true,
		.last_sleep_start = s_last_sleep_start,
		.last_sleep_end = s_last_sleep_end,
	};

	persist_write_data(PERSIST_KEY_TIMELINE_CYCLE, &p, sizeof(p));
}

void timeline_wake_persist_load(void) {
	TimelineCyclePersist p;

	if (persist_get_size(PERSIST_KEY_TIMELINE_CYCLE) != (int) sizeof(p)) {
		return;
	}
	if (persist_read_data(PERSIST_KEY_TIMELINE_CYCLE, &p, sizeof(p)) != sizeof(p)) {
		return;
	}
	if (p.magic != TIMELINE_PERSIST_MAGIC || !timeline_persist_fits_cycle(&p)) {
		return;
	}

	s_wake_utc = p.wake_utc;
	s_wake_valid = p.wake_valid;
	s_last_sleep_start = p.last_sleep_start;
	s_last_sleep_end = p.last_sleep_end;
}
#if defined(PBL_HEALTH)
static bool query_now_in_sleep_segment(void);
static bool timeline_peek_activity_sleep(void);
static bool health_data_unavailable(void);
static bool is_watch_on_wrist(void);
static bool is_user_awake(void);
static time_t find_last_sleep_end(void);
static bool find_merged_sleep_session(time_t *session_start, time_t *session_end);
static bool find_longest_sleep_segment(time_t *session_start, time_t *session_end);
static bool resolve_sleep_wake(time_t *session_start, time_t *session_end);
static bool wake_from_activity_after_bed(time_t *wake_utc_out);
static bool wake_from_step_quiet(time_t *wake_utc_out);
static bool wake_from_step_resume(time_t *wake_utc_out);
static bool wake_from_activity_bootstrap(time_t *wake_utc_out);
static time_t timeline_bed_utc(void);

static bool timeline_cycle_frozen(void) {
	if (!s_wake_valid) {
		return false;
	}

	const time_t now = time(NULL);
	if (now >= timeline_bed_utc() + TIMELINE_ACTIVITY_AFTER_BED_SEC
		|| query_now_in_sleep_segment()
		|| timeline_peek_activity_sleep()) {
		return false;
	}

	// Manual placeholder from a failed resolve should not block a real health wake.
	if (s_settings.wake_from_sleep && !health_data_unavailable()
			&& s_wake_utc == manual_wake_utc_for_now()) {
		return false;
	}

	return true;
}
static time_t get_wakeup_time(time_t *out_sleep_start, time_t *out_sleep_end);
void timeline_wake_run(void);
static bool recent_activity_data_present(int minutes);

static void merge_sleep_segments(SleepSegment *segments, int *count) {
	if (*count <= 1) {
		return;
	}

	int merged = 0;
	for (int i = 0; i < *count; i++) {
		if (merged == 0) {
			segments[merged++] = segments[i];
			continue;
		}

		SleepSegment *last = &segments[merged - 1];
		if (segments[i].start <= last->end + TIMELINE_SLEEP_MERGE_GAP_SEC) {
			if (segments[i].end > last->end) {
				last->end = segments[i].end;
			}
			if (segments[i].start < last->start) {
				last->start = segments[i].start;
			}
		} else {
			segments[merged++] = segments[i];
		}
	}

	*count = merged;
}

static void sort_sleep_segments_by_start(SleepSegment *segments, int count) {
	for (int i = 0; i < count - 1; i++) {
		for (int j = i + 1; j < count; j++) {
			if (segments[j].start < segments[i].start) {
				const SleepSegment tmp = segments[i];
				segments[i] = segments[j];
				segments[j] = tmp;
			}
		}
	}
}

typedef struct {
	SleepSegment segments[TIMELINE_MAX_SLEEP_SEGMENTS];
	int count;
} SleepCollectCtx;

static bool sleep_collect_cb(HealthActivity activity, time_t time_start, time_t time_end, void *context) {
	SleepCollectCtx *ctx = context;

	if (activity != HealthActivitySleep && activity != HealthActivityRestfulSleep) {
		return true;
	}

	if (time_end <= time_start) {
		return true;
	}

	if (ctx->count >= TIMELINE_MAX_SLEEP_SEGMENTS) {
		return false;
	}

	ctx->segments[ctx->count].start = time_start;
	ctx->segments[ctx->count].end = time_end;
	ctx->count++;
	return true;
}

static bool collect_sleep_segments(SleepCollectCtx *ctx, time_t search_start, time_t search_end) {
	memset(ctx, 0, sizeof(*ctx));

	HealthServiceAccessibilityMask mask = health_service_any_activity_accessible(
		HealthActivitySleep | HealthActivityRestfulSleep, search_start, search_end);
	if (!(mask & HealthServiceAccessibilityMaskAvailable)) {
		return false;
	}

	health_service_activities_iterate(
		HealthActivitySleep | HealthActivityRestfulSleep,
		search_start, search_end, HealthIterationDirectionPast,
		sleep_collect_cb, ctx);

	if (ctx->count == 0) {
		return false;
	}

	merge_sleep_segments(ctx->segments, &ctx->count);
	sort_sleep_segments_by_start(ctx->segments, ctx->count);
	return true;
}

static bool health_data_unavailable(void) {
	const time_t now = time(NULL);
	const HealthServiceAccessibilityMask mask = health_service_any_activity_accessible(
		HealthActivitySleep | HealthActivityRestfulSleep, now - SECONDS_PER_DAY, now);

	return (mask & HealthServiceAccessibilityMaskNotAvailable) != 0
		|| (mask & HealthServiceAccessibilityMaskNoPermission) != 0;
}

static bool minute_has_activity(const HealthMinuteData *m) {
	return !m->is_invalid && (m->steps > 0 || m->heart_rate_bpm > 0);
}

static bool minute_has_steps(const HealthMinuteData *m) {
	return !m->is_invalid && m->steps > 0;
}

static int timeline_local_hour(time_t utc) {
	time_t local = utc - (time_t) s_tz_offset_min * 60;
	struct tm *tm_ptr = gmtime(&local);
	if (!tm_ptr) {
		return 12;
	}
	return tm_ptr->tm_hour;
}

static bool timeline_morning_hour(int hour) {
	return hour >= TIMELINE_MORNING_WAKE_HOUR_START
		&& hour < TIMELINE_MORNING_WAKE_HOUR_END;
}

static bool recent_activity_in_last_minutes(int minutes) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	HealthMinuteData history[12];
	const time_t now = time(NULL);
	time_t time_start = now - (time_t) minutes * 60;
	time_t time_end = now;
	const uint32_t count = health_service_get_minute_history(
		history, 12, &time_start, &time_end);

	for (uint32_t i = 0; i < count; i++) {
		if (minute_has_activity(&history[i])) {
			return true;
		}
	}
#else
	(void) minutes;
#endif
	return false;
}

static int steps_since(time_t since_utc) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	const time_t now = time(NULL);
	if (since_utc >= now) {
		return 0;
	}

	HealthMinuteData history[TIMELINE_ACTIVITY_HISTORY_CHUNK];
	time_t cursor = since_utc;
	int total_steps = 0;

	while (cursor < now) {
		time_t chunk_end = cursor + (time_t) TIMELINE_ACTIVITY_HISTORY_CHUNK * 60;
		if (chunk_end > now) {
			chunk_end = now;
		}

		time_t time_start = cursor;
		time_t time_end = chunk_end;
		const uint32_t count = health_service_get_minute_history(
			history, TIMELINE_ACTIVITY_HISTORY_CHUNK, &time_start, &time_end);

		for (uint32_t i = 0; i < count; i++) {
			const time_t minute_utc = time_start + (time_t) i * 60;
			if (minute_utc < since_utc || minute_utc >= now) {
				continue;
			}
			if (history[i].is_invalid) {
				continue;
			}
			total_steps += history[i].steps;
		}

		if (count == 0) {
			cursor = chunk_end;
		} else {
			cursor = time_end;
		}
	}

	return total_steps;
#else
	(void) since_utc;
	return 0;
#endif
}

static bool is_user_awake(void) {
	if (recent_activity_in_last_minutes(TIMELINE_ON_WRIST_LOOKBACK_MIN)) {
		return true;
	}

	if (timeline_peek_activity_sleep()) {
		return false;
	}

	if (query_now_in_sleep_segment()) {
		return false;
	}

	const time_t last_sleep_end = find_last_sleep_end();
	if (last_sleep_end <= 0) {
		return true;
	}

	const time_t now = time(NULL);
	const time_t since = now - last_sleep_end;

	if (since < TIMELINE_AWAKE_RECENT_SLEEP_SEC) {
		return false;
	}

	if (since < TIMELINE_AWAKE_GRACE_SLEEP_SEC
			&& steps_since(last_sleep_end) < TIMELINE_AWAKE_GRACE_MAX_STEPS) {
		return false;
	}

	return true;
}

static bool is_watch_on_wrist(void) {
	return recent_activity_in_last_minutes(TIMELINE_ON_WRIST_LOOKBACK_MIN)
		|| recent_activity_data_present(TIMELINE_ON_WRIST_LOOKBACK_MIN);
}

// Any valid minute (even zero steps) means the watch is reporting data.
static bool recent_activity_data_present(int minutes) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	HealthMinuteData history[12];
	const time_t now = time(NULL);
	time_t time_start = now - (time_t) minutes * 60;
	time_t time_end = now;
	const uint32_t count = health_service_get_minute_history(
		history, 12, &time_start, &time_end);

	for (uint32_t i = 0; i < count; i++) {
		if (!history[i].is_invalid) {
			return true;
		}
	}
#else
	(void) minutes;
#endif
	return false;
}

static time_t find_last_sleep_end(void) {
	const time_t now = time(NULL);
	SleepCollectCtx ctx;

	if (!collect_sleep_segments(&ctx, now - TIMELINE_SLEEP_LOOKBACK_SEC, now)) {
		return 0;
	}

	time_t last_end = 0;
	for (int i = 0; i < ctx.count; i++) {
		if (ctx.segments[i].end <= now && ctx.segments[i].end > last_end) {
			last_end = ctx.segments[i].end;
		}
	}

	return last_end;
}

static bool find_merged_sleep_session(time_t *session_start, time_t *session_end) {
	const time_t now = time(NULL);

	if (query_now_in_sleep_segment() || timeline_peek_activity_sleep()) {
		return false;
	}

	const time_t search_start = now - TIMELINE_SLEEP_LOOKBACK_SEC;
	SleepCollectCtx ctx;
	if (!collect_sleep_segments(&ctx, search_start, now)) {
		return false;
	}

	int last_idx = -1;
	for (int i = 0; i < ctx.count; i++) {
		if (ctx.segments[i].end <= now
				&& (last_idx < 0 || ctx.segments[i].end > ctx.segments[last_idx].end)) {
			last_idx = i;
		}
	}

	if (last_idx < 0) {
		return false;
	}

	time_t merged_start = ctx.segments[last_idx].start;
	const time_t merged_end = ctx.segments[last_idx].end;
	int idx = last_idx;

	while (idx > 0) {
		const SleepSegment *prev = &ctx.segments[idx - 1];
		const SleepSegment *cur = &ctx.segments[idx];
		const int32_t total = (int32_t) (merged_end - merged_start);

		if (total >= TIMELINE_MIN_MERGED_SLEEP_SEC) {
			break;
		}
		if (prev->start < search_start) {
			break;
		}
		if (prev->end + TIMELINE_SLEEP_MERGE_GAP_SEC < cur->start) {
			break;
		}

		merged_start = prev->start;
		idx--;
	}

	if ((merged_end - merged_start) < TIMELINE_MIN_MERGED_SLEEP_SEC) {
		return false;
	}

	*session_start = merged_start;
	*session_end = merged_end;
	return true;
}

// When the API splits the night into chunks under 3h, still trust the longest recorded sleep.
static bool find_longest_sleep_segment(time_t *session_start, time_t *session_end) {
	const time_t now = time(NULL);

	if (query_now_in_sleep_segment() || timeline_peek_activity_sleep()) {
		return false;
	}

	const time_t search_start = now - TIMELINE_SLEEP_LOOKBACK_SEC;
	SleepCollectCtx ctx;
	if (!collect_sleep_segments(&ctx, search_start, now)) {
		return false;
	}

	int best_idx = -1;
	int32_t best_duration = 0;

	for (int i = 0; i < ctx.count; i++) {
		const int32_t duration = (int32_t) (ctx.segments[i].end - ctx.segments[i].start);
		if (duration < TIMELINE_MIN_SLEEP_SEGMENT_SEC) {
			continue;
		}
		if (ctx.segments[i].end > now) {
			continue;
		}
		if (duration > best_duration
				|| (duration == best_duration && best_idx >= 0
					&& ctx.segments[i].end > ctx.segments[best_idx].end)) {
			best_duration = duration;
			best_idx = i;
		}
	}

	if (best_idx < 0) {
		return false;
	}

	*session_start = ctx.segments[best_idx].start;
	*session_end = ctx.segments[best_idx].end;
	return true;
}

static bool resolve_sleep_wake(time_t *session_start, time_t *session_end) {
	if (find_merged_sleep_session(session_start, session_end)) {
		return true;
	}
	return find_longest_sleep_segment(session_start, session_end);
}

static time_t timeline_bed_utc(void) {
	return s_wake_utc + (time_t) awake_minutes() * 60;
}

static time_t timeline_activity_floor_utc(void) {
	if (s_wake_valid) {
		return timeline_bed_utc() + TIMELINE_ACTIVITY_AFTER_BED_SEC;
	}

	const time_t manual_wake = manual_wake_utc_for_now();
	return manual_wake + (time_t) awake_minutes() * 60 + TIMELINE_ACTIVITY_AFTER_BED_SEC;
}

static time_t sleep_activity_floor_utc(void) {
	time_t floor = timeline_activity_floor_utc();

	time_t sleep_start = 0;
	time_t sleep_end = 0;
	if (find_longest_sleep_segment(&sleep_start, &sleep_end)) {
		const time_t after_sleep = sleep_end + TIMELINE_AWAKE_RECENT_SLEEP_SEC;
		if (after_sleep > floor) {
			floor = after_sleep;
		}
	}

	return floor;
}

// When sleep API is empty, find wake as first step streak after a long quiet night.
static bool wake_from_step_quiet(time_t *wake_utc_out) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	const time_t now = time(NULL);
	const time_t scan_start = now - TIMELINE_SLEEP_LOOKBACK_SEC;
	if (scan_start >= now) {
		return false;
	}

	HealthMinuteData history[TIMELINE_ACTIVITY_HISTORY_CHUNK];
	time_t cursor = scan_start;
	time_t last_streak_end = scan_start;
	int streak = 0;
	time_t streak_start = 0;
	int32_t best_quiet_sec = 0;
	time_t best_wake = 0;
	bool best_morning = false;

	while (cursor < now) {
		time_t chunk_end = cursor + (time_t) TIMELINE_ACTIVITY_HISTORY_CHUNK * 60;
		if (chunk_end > now) {
			chunk_end = now;
		}

		time_t time_start = cursor;
		time_t time_end = chunk_end;
		const uint32_t count = health_service_get_minute_history(
			history, TIMELINE_ACTIVITY_HISTORY_CHUNK, &time_start, &time_end);

		for (uint32_t i = 0; i < count; i++) {
			const time_t minute_utc = time_start + (time_t) i * 60;
			if (minute_utc >= now) {
				break;
			}

			if (minute_has_steps(&history[i])) {
				if (streak > 0
						&& minute_utc == streak_start + (time_t) streak * 60) {
					streak++;
				} else {
					streak = 1;
					streak_start = minute_utc;
				}

				if (streak >= TIMELINE_WAKE_CONFIRM_MINUTES) {
					const int32_t quiet_sec = (int32_t) (streak_start - last_streak_end);
					const bool morning = timeline_morning_hour(timeline_local_hour(streak_start));
					const bool better = quiet_sec > best_quiet_sec
						|| (quiet_sec == best_quiet_sec && morning && !best_morning)
						|| (quiet_sec == best_quiet_sec && morning == best_morning
							&& streak_start > best_wake);

					if (quiet_sec >= TIMELINE_MIN_STEP_REST_QUIET_SEC && better) {
						best_quiet_sec = quiet_sec;
						best_wake = streak_start;
						best_morning = morning;
					}
				}
			} else {
				if (streak >= TIMELINE_WAKE_CONFIRM_MINUTES) {
					last_streak_end = streak_start + (time_t) streak * 60;
				}
				streak = 0;
			}
		}

		cursor = time_end;
	}

	if (best_wake <= 0 || best_wake >= now) {
		return false;
	}

	*wake_utc_out = best_wake;
	return true;
#else
	(void) wake_utc_out;
	return false;
#endif
}

// After leaving the watch off-wrist/table: first steps following hours without steps.
static bool wake_from_step_resume(time_t *wake_utc_out) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	const time_t now = time(NULL);
	const time_t scan_start = now - TIMELINE_STEP_RESUME_LOOKBACK_SEC;
	if (scan_start >= now) {
		return false;
	}

	HealthMinuteData history[TIMELINE_ACTIVITY_HISTORY_CHUNK];
	time_t cursor = scan_start;
	time_t last_step_minute = 0;
	time_t best_wake = 0;

	while (cursor < now) {
		time_t chunk_end = cursor + (time_t) TIMELINE_ACTIVITY_HISTORY_CHUNK * 60;
		if (chunk_end > now) {
			chunk_end = now;
		}

		time_t time_start = cursor;
		time_t time_end = chunk_end;
		const uint32_t count = health_service_get_minute_history(
			history, TIMELINE_ACTIVITY_HISTORY_CHUNK, &time_start, &time_end);

		for (uint32_t i = 0; i < count; i++) {
			const time_t minute_utc = time_start + (time_t) i * 60;
			if (minute_utc >= now) {
				break;
			}

			if (minute_has_steps(&history[i])) {
				if (last_step_minute > 0) {
					const int32_t quiet_sec = (int32_t) (minute_utc - (last_step_minute + 60));
					if (quiet_sec >= TIMELINE_MIN_STEP_REST_QUIET_SEC) {
						best_wake = minute_utc;
					}
				}
				last_step_minute = minute_utc;
			}
		}

		cursor = time_end;
	}

	if (best_wake <= 0 || best_wake >= now) {
		return false;
	}

	*wake_utc_out = best_wake;
	return true;
#else
	(void) wake_utc_out;
	return false;
#endif
}

static bool wake_from_activity_after_bed(time_t *wake_utc_out) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	const time_t now = time(NULL);
	const time_t floor = sleep_activity_floor_utc();

	if (floor >= now) {
		return false;
	}

	HealthMinuteData history[TIMELINE_ACTIVITY_HISTORY_CHUNK];
	time_t cursor = floor;
	int streak = 0;
	time_t streak_start = 0;

	while (cursor < now) {
		time_t chunk_end = cursor + (time_t) TIMELINE_ACTIVITY_HISTORY_CHUNK * 60;
		if (chunk_end > now) {
			chunk_end = now;
		}

		time_t time_start = cursor;
		time_t time_end = chunk_end;
		const uint32_t count = health_service_get_minute_history(
			history, TIMELINE_ACTIVITY_HISTORY_CHUNK, &time_start, &time_end);

		debug_profile_add(DBG_TIMELINE_WAKE_SCAN_MIN, count);

		if (count == 0) {
			streak = 0;
			cursor = chunk_end;
			continue;
		}

		for (uint32_t i = 0; i < count; i++) {
			const time_t minute_utc = time_start + (time_t) i * 60;
			if (minute_utc < floor || minute_utc >= now) {
				continue;
			}

			if (minute_has_steps(&history[i])) {
				if (streak > 0
						&& minute_utc == streak_start + (time_t) streak * 60) {
					streak++;
				} else {
					streak = 1;
					streak_start = minute_utc;
				}

				if (streak >= TIMELINE_WAKE_CONFIRM_MINUTES) {
					*wake_utc_out = streak_start;
					return true;
				}
			} else {
				streak = 0;
			}
		}

		cursor = time_end;
	}

	return false;
#else
	(void) wake_utc_out;
	return false;
#endif
}

typedef struct {
	int32_t best_gap_sec;
	time_t best_gap_end;
	time_t wake_after_best;
	bool in_gap;
	time_t gap_start;
	int pending_streak;
	time_t pending_start;
} OffWristScanCtx;

static void bootstrap_try_finish_gap(OffWristScanCtx *ctx) {
	if (!ctx->in_gap || ctx->pending_streak < 1) {
		return;
	}

	const time_t gap_end = ctx->pending_start;
	const int32_t gap_sec = (int32_t) (gap_end - ctx->gap_start);
	if (gap_sec < TIMELINE_MIN_OFF_WRIST_SEC) {
		return;
	}

	const int min_streak = gap_sec >= TIMELINE_LONG_GAP_WAKE_SEC
		? 1
		: TIMELINE_WAKE_CONFIRM_MINUTES;
	if (ctx->pending_streak < min_streak) {
		return;
	}

	if (gap_sec > ctx->best_gap_sec
			|| (gap_sec == ctx->best_gap_sec && gap_end > ctx->best_gap_end)) {
		ctx->best_gap_sec = gap_sec;
		ctx->best_gap_end = gap_end;
		ctx->wake_after_best = ctx->pending_start;
	}

	ctx->in_gap = false;
	ctx->pending_streak = 0;
}

static bool bootstrap_skip_post_wake_gap(const OffWristScanCtx *ctx, time_t minute_utc) {
	return ctx->wake_after_best > 0
		&& minute_utc >= ctx->wake_after_best
		&& minute_utc < ctx->wake_after_best + TIMELINE_POST_WAKE_GAP_COOLDOWN_SEC;
}

// After reinstall or stale persist: scan recent history for real wake (off-wrist gaps).
static bool wake_from_activity_bootstrap(time_t *wake_utc_out) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	const time_t now = time(NULL);
	const time_t max_span = TIMELINE_SLEEP_LOOKBACK_SEC + (time_t) awake_minutes() * 60;
	const time_t sleep_anchor = find_last_sleep_end();
	const time_t scan_start = now > max_span ? now - max_span : 0;
	time_t cursor_start = scan_start;

	if (sleep_anchor > 0) {
		const time_t after_sleep = sleep_anchor + TIMELINE_AWAKE_RECENT_SLEEP_SEC;
		if (after_sleep > cursor_start) {
			cursor_start = after_sleep;
		}
	}

	if (cursor_start >= now) {
		return false;
	}

	HealthMinuteData history[TIMELINE_ACTIVITY_HISTORY_CHUNK];
	time_t cursor = cursor_start;
	OffWristScanCtx ctx;
	memset(&ctx, 0, sizeof(ctx));

	while (cursor < now) {
		time_t chunk_end = cursor + (time_t) TIMELINE_ACTIVITY_HISTORY_CHUNK * 60;
		if (chunk_end > now) {
			chunk_end = now;
		}

		time_t time_start = cursor;
		time_t time_end = chunk_end;
		const uint32_t count = health_service_get_minute_history(
			history, TIMELINE_ACTIVITY_HISTORY_CHUNK, &time_start, &time_end);

		debug_profile_add(DBG_TIMELINE_WAKE_SCAN_MIN, count);

		if (count == 0) {
			if (!ctx.in_gap) {
				ctx.gap_start = cursor;
				ctx.in_gap = true;
			}
			cursor = chunk_end;
			continue;
		}

		if (time_start > cursor && !ctx.in_gap) {
			ctx.gap_start = cursor;
			ctx.in_gap = true;
		}

		for (uint32_t i = 0; i < count; i++) {
			const time_t minute_utc = time_start + (time_t) i * 60;
			if (minute_utc >= now) {
				break;
			}

			if (bootstrap_skip_post_wake_gap(&ctx, minute_utc)) {
				continue;
			}

			if (minute_has_steps(&history[i])) {
				if (!ctx.in_gap) {
					continue;
				}
				if (ctx.pending_streak > 0
						&& minute_utc == ctx.pending_start + (time_t) ctx.pending_streak * 60) {
					ctx.pending_streak++;
				} else {
					ctx.pending_streak = 1;
					ctx.pending_start = minute_utc;
				}
				bootstrap_try_finish_gap(&ctx);
			} else if (!ctx.in_gap) {
				ctx.gap_start = minute_utc;
				ctx.in_gap = true;
				ctx.pending_streak = 0;
			} else {
				ctx.pending_streak = 0;
			}
		}

		cursor = time_end;
	}

	if (ctx.in_gap) {
		bootstrap_try_finish_gap(&ctx);
	}

	if (ctx.wake_after_best <= 0 || ctx.wake_after_best >= now) {
		return false;
	}

	*wake_utc_out = ctx.wake_after_best;
	return true;
#else
	(void) wake_utc_out;
	return false;
#endif
}

bool timeline_wake_settings_relevant_diff(const TimelineSettings *prev, const TimelineSettings *next) {
	return prev->sleep_hours_x10 != next->sleep_hours_x10
		|| prev->wake_from_sleep != next->wake_from_sleep
		|| prev->manual_wake_min != next->manual_wake_min;
}

static time_t get_wakeup_time(time_t *out_sleep_start, time_t *out_sleep_end) {
	if (out_sleep_start) {
		*out_sleep_start = 0;
	}
	if (out_sleep_end) {
		*out_sleep_end = 0;
	}

	if (!s_settings.wake_from_sleep || health_data_unavailable()) {
		return manual_wake_utc_for_now();
	}

	// Peek-sleep can block step-resume wake detection; see analyze_wake.py.
	if (query_now_in_sleep_segment() || timeline_peek_activity_sleep()) {
		return s_wake_valid ? s_wake_utc : manual_wake_utc_for_now();
	}

	const time_t last_sleep = find_last_sleep_end();
	time_t session_start = 0;
	time_t session_end = 0;
	const bool has_sleep = resolve_sleep_wake(&session_start, &session_end);

	if (has_sleep && s_wake_valid && session_end != s_wake_utc
			&& labs((int) (session_end - s_wake_utc)) >= TIMELINE_AWAKE_RECENT_SLEEP_SEC) {
		if (out_sleep_start) {
			*out_sleep_start = session_start;
		}
		if (out_sleep_end) {
			*out_sleep_end = session_end;
		}
		return session_end;
	}

	if (last_sleep > 0 && last_sleep == s_last_sleep_end && timeline_cycle_frozen()) {
		return s_wake_utc;
	}

	if (timeline_cycle_frozen()) {
		return s_wake_utc;
	}

	if (has_sleep) {
		if (out_sleep_start) {
			*out_sleep_start = session_start;
		}
		if (out_sleep_end) {
			*out_sleep_end = session_end;
		}
		return session_end;
	}

	time_t activity_wake = 0;
	if (wake_from_step_resume(&activity_wake)) {
		return activity_wake;
	}

	if (wake_from_step_quiet(&activity_wake)) {
		return activity_wake;
	}

	if (wake_from_activity_after_bed(&activity_wake)) {
		return activity_wake;
	}

	if (!s_wake_valid && wake_from_activity_bootstrap(&activity_wake)) {
		return activity_wake;
	}

	return manual_wake_utc_for_now();
}

void timeline_wake_invalidate(void) {
	s_last_sleep_start = 0;
	s_last_sleep_end = 0;
	s_wake_valid = false;
	s_wake_utc = 0;
	timeline_wake_persist_clear();
}

static void timeline_commit_wake(time_t wake, time_t sleep_start, bool from_sleep) {
	s_wake_utc = wake;
	s_wake_valid = true;
	s_last_sleep_start = sleep_start;
	s_last_sleep_end = find_last_sleep_end();
	timeline_wake_persist_save();
	if (s_hooks.on_wake_changed) {
		s_hooks.on_wake_changed();
	}
	debug_profile_count(from_sleep ? DBG_TIMELINE_WAKE_SLEEP : DBG_TIMELINE_WAKE_ACTIVITY);
}

void timeline_wake_run(void) {
	if (!s_settings.show) {
		return;
	}

	if (!s_settings.wake_from_sleep || health_data_unavailable()) {
		const time_t manual_wake = manual_wake_utc_for_now();
		if (!s_wake_valid || s_wake_utc != manual_wake) {
			s_last_sleep_start = 0;
			s_last_sleep_end = 0;
			s_wake_utc = manual_wake;
			s_wake_valid = true;
			timeline_wake_persist_save();
			if (s_hooks.on_wake_changed) {
				s_hooks.on_wake_changed();
			}
			debug_profile_count(DBG_TIMELINE_WAKE_MANUAL);
		}
		return;
	}

	if (!s_wake_valid) {
		time_t sleep_start = 0;
		time_t sleep_end = 0;
		const time_t wake = get_wakeup_time(&sleep_start, &sleep_end);
		timeline_commit_wake(wake, sleep_start, sleep_end > 0);
		return;
	}

	if (!is_watch_on_wrist() || !is_user_awake()) {
		return;
	}

	time_t sleep_start = 0;
	time_t sleep_end = 0;
	const time_t wake = get_wakeup_time(&sleep_start, &sleep_end);

	if (wake != s_wake_utc) {
		timeline_commit_wake(wake, sleep_start, sleep_end > 0);
	}
}

void timeline_wake_on_sleep_update(void) {
	s_last_sleep_start = 0;
	s_last_sleep_end = 0;
	timeline_wake_run();
}

bool timeline_wake_marker_updates_allowed(void) {
	if (!s_wake_valid) {
		return false;
	}

	if (!s_settings.wake_from_sleep || health_data_unavailable()) {
		return true;
	}

	return is_watch_on_wrist() && is_user_awake();
}

// Instantaneous activity flags — often stale or set off-wrist; not the same as sleep graph data.
static bool timeline_peek_activity_sleep(void) {
	HealthActivityMask activities = health_service_peek_current_activities();
	return (activities & (HealthActivitySleep | HealthActivityRestfulSleep)) != 0;
}

typedef struct {
	time_t now;
	bool inside;
} SegmentNowCtx;

static bool segment_now_cb(HealthActivity activity, time_t time_start, time_t time_end,
		void *context) {
	(void) activity;
	SegmentNowCtx *ctx = context;

	if (time_start <= ctx->now && ctx->now < time_end) {
		ctx->inside = true;
		return false;
	}

	return true;
}

static bool query_now_in_sleep_segment(void) {
	const time_t now = time(NULL);
	const time_t search_start = now - SECONDS_PER_DAY;
	const time_t search_end = now + 60;
	HealthServiceAccessibilityMask mask = health_service_any_activity_accessible(
		HealthActivitySleep | HealthActivityRestfulSleep, search_start, search_end);

	if (!(mask & HealthServiceAccessibilityMaskAvailable)) {
		return false;
	}

	SegmentNowCtx ctx = {
		.now = now,
		.inside = false,
	};

	health_service_activities_iterate(
		HealthActivitySleep | HealthActivityRestfulSleep,
		search_start, search_end,
		HealthIterationDirectionPast,
		segment_now_cb, &ctx);

	return ctx.inside;
}

bool timeline_wake_user_is_sleeping(void) {
	return !is_user_awake();
}
#endif

void timeline_wake_set_hooks(const TimelineWakeHooks *hooks) {
	if (hooks) {
		s_hooks = *hooks;
	} else {
		memset(&s_hooks, 0, sizeof(s_hooks));
	}
}

void timeline_wake_bootstrap(int32_t tz_offset_min, const TimelineSettings *settings) {
	if (settings) {
		s_settings = *settings;
	}
	s_tz_offset_min = tz_offset_min;
}

void timeline_wake_set_tz_offset(int32_t tz_offset_min) {
	s_tz_offset_min = tz_offset_min;
}

void timeline_wake_set_settings(const TimelineSettings *settings) {
	if (settings) {
		s_settings = *settings;
	}
}

void timeline_wake_snapshot(TimelineWakeSnapshot *out) {
	if (!out) {
		return;
	}
	out->wake_utc = s_wake_utc;
	out->wake_valid = s_wake_valid;
}

int32_t timeline_wake_awake_minutes(const TimelineSettings *settings) {
	const TimelineSettings *cfg = settings ? settings : &s_settings;
	int32_t sleep_min = (cfg->sleep_hours_x10 * 60) / 10;
	int32_t awake = (24 * 60) - sleep_min;
	return awake > 0 ? awake : 1;
}

void timeline_wake_format_local(time_t utc_timestamp, int32_t tz_offset_min, char *buffer,
		size_t buffer_size) {
	if (utc_timestamp <= 0) {
		snprintf(buffer, buffer_size, "--:--");
		return;
	}

	time_t local = utc_timestamp - (time_t) tz_offset_min * 60;
	struct tm *tm_ptr = gmtime(&local);
	if (!tm_ptr) {
		snprintf(buffer, buffer_size, "--:--");
		return;
	}

	strftime(buffer, buffer_size, "%H:%M", tm_ptr);
}
