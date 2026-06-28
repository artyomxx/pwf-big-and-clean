#include "debug_profile.h"

#if DEBUG_PROFILE

#include <pebble.h>
#include <string.h>

#include "message_keys.auto.h"
#include "util.h"

#if defined(PBL_HEALTH)
#include "health_export.h"
#include "settings.h"
#endif

static uint32_t s_counts[DBG_COUNT];
static time_t s_session_start;

static void log_snapshot(uint32_t elapsed_sec) {
	APP_LOG(APP_LOG_LEVEL_INFO,
		"profile +%us: tick:%u skip:%u health:%u hskip:%u layout:%u lcache:%u",
		elapsed_sec,
		s_counts[DBG_TICK], s_counts[DBG_TICK_SLEEP_SKIP],
		s_counts[DBG_HEALTH_EVENT], s_counts[DBG_HEALTH_SLEEP_SKIP],
		s_counts[DBG_CENTER_LAYOUT], s_counts[DBG_CENTER_LAYOUT_CACHED]);
	APP_LOG(APP_LOG_LEVEL_INFO,
		"profile +%us: tl_ref:%u tl_lay:%u tl_dirty:%u wake s:%u a:%u m:%u scan:%u",
		elapsed_sec,
		s_counts[DBG_TIMELINE_REFRESH], s_counts[DBG_TIMELINE_LAYOUT_CB],
		s_counts[DBG_TIMELINE_DIRTY],
		s_counts[DBG_TIMELINE_WAKE_SLEEP], s_counts[DBG_TIMELINE_WAKE_ACTIVITY],
		s_counts[DBG_TIMELINE_WAKE_MANUAL],
		s_counts[DBG_TIMELINE_WAKE_SCAN_MIN]);
	APP_LOG(APP_LOG_LEVEL_INFO,
		"profile +%us: solar chk:%u run:%u phone:%u settings:%u skip:%u conn:%u",
		elapsed_sec,
		s_counts[DBG_SOLAR_MAYBE_REFRESH], s_counts[DBG_SOLAR_SCHEDULED],
		s_counts[DBG_SOLAR_PHONE_REQ],
		s_counts[DBG_SETTINGS_APPLY], s_counts[DBG_SETTINGS_SKIPPED],
		s_counts[DBG_CONNECTION]);
}

static uint32_t session_elapsed_sec(void) {
	const time_t now = time(NULL);
	if (s_session_start <= 0 || now <= s_session_start) {
		return 0;
	}
	return (uint32_t) (now - s_session_start);
}

static void send_profile_ack(int32_t cmd, uint32_t elapsed_sec) {
	DictionaryIterator *iter;
	const AppMessageResult result = app_message_outbox_begin(&iter);

	if (result != APP_MSG_OK) {
		APP_LOG(APP_LOG_LEVEL_WARNING, "profile ack outbox fail %d", (int) result);
		return;
	}

	dict_write_int32(iter, MESSAGE_KEY_DbgProfileAck, cmd);
	dict_write_int32(iter, MESSAGE_KEY_DbgProfileElapsed, (int32_t) elapsed_sec);
	dict_write_int32(iter, MESSAGE_KEY_DbgProfileTick, (int32_t) s_counts[DBG_TICK]);
	app_message_outbox_send();
}

void debug_profile_reset(void) {
	memset(s_counts, 0, sizeof(s_counts));
	s_session_start = time(NULL);
	APP_LOG(APP_LOG_LEVEL_INFO, "profile reset (collecting)");
	send_profile_ack(DBG_PROFILE_CMD_RESET, 0);
}

void debug_profile_dump(void) {
	const uint32_t elapsed = session_elapsed_sec();
	log_snapshot(elapsed);
	APP_LOG(APP_LOG_LEVEL_INFO, "profile dump done (+%us, counters kept)", elapsed);
	send_profile_ack(DBG_PROFILE_CMD_DUMP, elapsed);
}

bool debug_profile_handle_message(void *iterator) {
	DictionaryIterator *iter = iterator;
	Tuple *tuple;

	if (!iter) {
		return false;
	}

	tuple = dict_find(iter, MESSAGE_KEY_DbgProfileCmd);
	if (!tuple) {
		return false;
	}

	const int32_t cmd = util_tuple_read_int32(tuple);
	APP_LOG(APP_LOG_LEVEL_INFO, "profile cmd %d", (int) cmd);

	if (cmd == DBG_PROFILE_CMD_RESET) {
		debug_profile_reset();
	} else if (cmd == DBG_PROFILE_CMD_DUMP) {
		debug_profile_dump();
#if defined(PBL_HEALTH)
	} else if (cmd == DBG_PROFILE_CMD_HEALTH_EXPORT) {
		health_export_dump(settings_tz_offset_min());
		APP_LOG(APP_LOG_LEVEL_INFO, "profile: health export done (see [health] lines)");
#endif
	} else {
		APP_LOG(APP_LOG_LEVEL_WARNING,
			"profile: unknown DbgProfileCmd %d (1=reset 2=dump 3=health)", (int) cmd);
	}

	return true;
}

void debug_profile_init(void) {
	memset(s_counts, 0, sizeof(s_counts));
	s_session_start = time(NULL);
	APP_LOG(APP_LOG_LEVEL_INFO,
		"profile on: DbgProfileCmd key %u (1=reset 2=dump 3=health)",
		(unsigned) MESSAGE_KEY_DbgProfileCmd);
}

void debug_profile_deinit(void) {
}

void debug_profile_count(DebugTag tag) {
	if (tag < DBG_COUNT) {
		s_counts[tag]++;
	}
}

void debug_profile_add(DebugTag tag, uint32_t amount) {
	if (tag < DBG_COUNT) {
		s_counts[tag] += amount;
	}
}

#endif
