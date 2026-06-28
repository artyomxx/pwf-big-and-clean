#include "config.h"
#include "health_export.h"

#if DEBUG_PROFILE

#include "timeline_wake.h"

#include <pebble.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define HEALTH_EXPORT_HOURS 40
#define HEALTH_EXPORT_MINUTE_CHUNK 8
#define HEALTH_EXPORT_SLEEP_MAX 32

typedef struct {
	time_t start;
	time_t end;
	int activity;
} ExportSleepSegment;

typedef struct {
	ExportSleepSegment segments[HEALTH_EXPORT_SLEEP_MAX];
	int count;
} ExportSleepCtx;

static time_t export_local_midnight_utc(time_t now, int32_t tz_offset_min) {
	time_t local = now - (time_t) tz_offset_min * 60;
	struct tm *tm_ptr = gmtime(&local);
	if (!tm_ptr) {
		return now;
	}

	time_t local_midnight = local - (time_t) tm_ptr->tm_hour * 3600
		- (time_t) tm_ptr->tm_min * 60 - (time_t) tm_ptr->tm_sec;
	return local_midnight + (time_t) tz_offset_min * 60;
}

static void export_merge_sleep_segments(ExportSleepSegment *segments, int *count) {
	if (*count <= 1) {
		return;
	}

	int merged = 0;
	for (int i = 0; i < *count; i++) {
		if (merged == 0) {
			segments[merged++] = segments[i];
			continue;
		}

		ExportSleepSegment *last = &segments[merged - 1];
		if (segments[i].start <= last->end + 15 * 60) {
			if (segments[i].end > last->end) {
				last->end = segments[i].end;
			}
		} else {
			segments[merged++] = segments[i];
		}
	}

	*count = merged;
}

static bool export_sleep_collect_cb(HealthActivity activity, time_t time_start, time_t time_end,
		void *context) {
	ExportSleepCtx *ctx = context;

	if (activity != HealthActivitySleep && activity != HealthActivityRestfulSleep) {
		return true;
	}
	if (time_end <= time_start || ctx->count >= HEALTH_EXPORT_SLEEP_MAX) {
		return ctx->count < HEALTH_EXPORT_SLEEP_MAX;
	}

	ctx->segments[ctx->count].start = time_start;
	ctx->segments[ctx->count].end = time_end;
	ctx->segments[ctx->count].activity = (int) activity;
	ctx->count++;
	return ctx->count < HEALTH_EXPORT_SLEEP_MAX;
}

static void export_sleep_segments(time_t scan_start, time_t now) {
#ifdef _PBL_API_EXISTS_health_service_activities_iterate
	HealthServiceAccessibilityMask mask = health_service_any_activity_accessible(
		HealthActivitySleep | HealthActivityRestfulSleep, scan_start, now);
	if (!(mask & HealthServiceAccessibilityMaskAvailable)) {
		APP_LOG(APP_LOG_LEVEL_INFO, "[health] sleep none");
		return;
	}

	ExportSleepCtx ctx;
	memset(&ctx, 0, sizeof(ctx));
	health_service_activities_iterate(
		HealthActivitySleep | HealthActivityRestfulSleep,
		scan_start, now, HealthIterationDirectionPast,
		export_sleep_collect_cb, &ctx);

	if (ctx.count == 0) {
		APP_LOG(APP_LOG_LEVEL_INFO, "[health] sleep none");
		return;
	}

	export_merge_sleep_segments(ctx.segments, &ctx.count);
	APP_LOG(APP_LOG_LEVEL_INFO, "[health] sleep count %d", ctx.count);

	for (int i = 0; i < ctx.count; i++) {
		const int32_t duration = (int32_t) (ctx.segments[i].end - ctx.segments[i].start);
		APP_LOG(APP_LOG_LEVEL_INFO,
			"[health] sleep %ld %ld %d %ld",
			(long) ctx.segments[i].start,
			(long) ctx.segments[i].end,
			ctx.segments[i].activity,
			(long) duration);
	}
#else
	APP_LOG(APP_LOG_LEVEL_INFO, "[health] sleep unsupported");
#endif
}

static void export_minute_history(time_t scan_start, time_t now) {
#ifdef _PBL_API_EXISTS_health_service_get_minute_history
	HealthMinuteData history[TIMELINE_ACTIVITY_HISTORY_CHUNK];
	time_t cursor = scan_start;
	uint32_t total = 0;

	while (cursor < now) {
		time_t chunk_end = cursor + (time_t) TIMELINE_ACTIVITY_HISTORY_CHUNK * 60;
		if (chunk_end > now) {
			chunk_end = now;
		}

		time_t time_start = cursor;
		time_t time_end = chunk_end;
		const uint32_t count = health_service_get_minute_history(
			history, TIMELINE_ACTIVITY_HISTORY_CHUNK, &time_start, &time_end);

		if (count == 0) {
			APP_LOG(APP_LOG_LEVEL_INFO,
				"[health] gap %ld %ld",
				(long) cursor,
				(long) chunk_end);
			cursor = chunk_end;
			continue;
		}

		if (time_start > cursor) {
			APP_LOG(APP_LOG_LEVEL_INFO,
				"[health] gap %ld %ld",
				(long) cursor,
				(long) time_start);
		}

		for (uint32_t i = 0; i < count; i += HEALTH_EXPORT_MINUTE_CHUNK) {
			char line[192];
			int offset = snprintf(line, sizeof(line), "[health] min %ld",
				(long) (time_start + (time_t) i * 60));
			const uint32_t batch = (i + HEALTH_EXPORT_MINUTE_CHUNK <= count)
				? HEALTH_EXPORT_MINUTE_CHUNK
				: (count - i);

			for (uint32_t j = 0; j < batch && offset > 0 && offset < (int) sizeof(line) - 24; j++) {
				const HealthMinuteData *m = &history[i + j];
				offset += snprintf(line + offset, sizeof(line) - (size_t) offset,
					" %u,%u,%u,%u",
					m->steps,
					m->heart_rate_bpm,
					m->vmc,
					m->is_invalid ? 1U : 0U);
			}

			APP_LOG(APP_LOG_LEVEL_INFO, "%s", line);
			total += batch;
		}

		cursor = time_end;
	}

	APP_LOG(APP_LOG_LEVEL_INFO, "[health] minute_total %lu", (unsigned long) total);
#else
	APP_LOG(APP_LOG_LEVEL_INFO, "[health] minute unsupported");
#endif
}

void health_export_dump(int32_t tz_offset_min) {
	const time_t now = time(NULL);
	const time_t scan_start = now - (time_t) HEALTH_EXPORT_HOURS * 3600;
	const time_t day_start = export_local_midnight_utc(now, tz_offset_min);

	APP_LOG(APP_LOG_LEVEL_INFO,
		"[health] meta now=%ld tz=%ld scan=%ld day=%ld",
		(long) now,
		(long) tz_offset_min,
		(long) scan_start,
		(long) day_start);
	APP_LOG(APP_LOG_LEVEL_INFO,
		"[health] cfg merged_sleep_h=%d merge_gap_min=%d lookback_h=%d after_bed_h=%d confirm_min=%d",
		TIMELINE_MIN_MERGED_SLEEP_SEC / 3600,
		TIMELINE_SLEEP_MERGE_GAP_SEC / 60,
		TIMELINE_SLEEP_LOOKBACK_SEC / 3600,
		TIMELINE_ACTIVITY_AFTER_BED_SEC / 3600,
		TIMELINE_WAKE_CONFIRM_MINUTES);

	export_sleep_segments(scan_start, now);
	export_minute_history(scan_start, now);
	APP_LOG(APP_LOG_LEVEL_INFO, "[health] done");
}

#endif
