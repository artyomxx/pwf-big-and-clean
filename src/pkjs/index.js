var Clay = require('@rebble/clay')
var clayConfig = require('./config')
var customClay = require('./custom-clay')
var messageKeys = require('message_keys')

// PKJS: Clay default handler breaks on cancel; we send int-only settings ourselves.
var clay = new Clay(clayConfig, customClay, { autoHandleEvents: false })

var GEO_OPTIONS = {
	maximumAge: 300000,
	timeout: 15000,
	enableHighAccuracy: true
}

var cachedAltitudeM = 0

function formatCoord(value) {
	return String(Math.round(value * 100000) / 100000)
}

function phoneTimezoneOffsetMin() {
	return new Date().getTimezoneOffset()
}

function readStoredClaySettings() {
	try {
		return JSON.parse(localStorage.getItem('clay-settings') || '{}')
	} catch (e) {
		return {}
	}
}

function loadCachedAltitude() {
	try {
		var stored = parseInt(localStorage.getItem('cached-altitude-m'), 10)
		if (isFinite(stored) && stored > 0) {
			cachedAltitudeM = stored
		}
	} catch (e) {
	}
}

function saveCachedAltitude(altitudeM) {
	if (!altitudeM || altitudeM <= 0) {
		return
	}
	cachedAltitudeM = altitudeM
	try {
		localStorage.setItem('cached-altitude-m', String(altitudeM))
	} catch (e) {
	}
}

function getClaySetting(settings, key) {
	var item = settings[key]
	if (item === undefined || item === null) {
		var numericKey = messageKeys[key]
		if (numericKey !== undefined) {
			item = settings[numericKey]
		}
	}
	if (item === undefined || item === null) {
		return undefined
	}
	if (typeof item === 'object' && Object.prototype.hasOwnProperty.call(item, 'value')) {
		return item.value
	}
	return item
}

function isGpsEnabled(stored) {
	var value = getClaySetting(stored, 'UsePhoneGPS')
	return value === true || value === 1 || value === '1'
}

function isDisplayEnabled(stored, key, defaultValue) {
	if (defaultValue === undefined) {
		defaultValue = true
	}
	var value = getClaySetting(stored, key)
	if (value === undefined || value === null) {
		return defaultValue
	}
	return value === true || value === 1 || value === '1'
}

function parsePositiveInt(value, fallback) {
	var parsed = parseInt(value, 10)
	if (!isFinite(parsed) || parsed < 0) {
		return fallback
	}
	return parsed
}

function parseSleepHoursX10(stored) {
	var raw = getClaySetting(stored, 'SleepHours')
	var hours = parseFloat(raw)
	if (!isFinite(hours) || hours <= 0 || hours > 16) {
		return 80
	}
	return Math.round(hours * 10)
}

function parseTimeToMinutes(value, fallback) {
	if (value === undefined || value === null || value === '') {
		return fallback
	}
	var parts = String(value).split(':')
	var hours = parseInt(parts[0], 10)
	var minutes = parseInt(parts[1], 10)
	if (!isFinite(hours)) {
		return fallback
	}
	if (!isFinite(minutes)) {
		minutes = 0
	}
	return hours * 60 + minutes
}

function parseDateFormat(stored) {
	var raw = getClaySetting(stored, 'DateFormat')
	var parsed = parseInt(raw, 10)
	if (!isFinite(parsed) || parsed < 0 || parsed > 10) {
		return 0
	}
	return parsed
}

function sendSettingsToWatch(stored, retries) {
	retries = retries || 0

	if (typeof Pebble.sendAppMessage !== 'function') {
		return
	}

	var dict = {}
	dict[messageKeys.ShowSteps] = isDisplayEnabled(stored, 'ShowSteps') ? 1 : 0
	dict[messageKeys.ShowBpm] = isDisplayEnabled(stored, 'ShowBpm') ? 1 : 0
	dict[messageKeys.ShowSun] = isDisplayEnabled(stored, 'ShowSun') ? 1 : 0
	dict[messageKeys.ShowMoon] = isDisplayEnabled(stored, 'ShowMoon') ? 1 : 0
	dict[messageKeys.ShowBattery] = isDisplayEnabled(stored, 'ShowBattery') ? 1 : 0
	dict[messageKeys.ShowLabels] = isDisplayEnabled(stored, 'ShowLabels') ? 1 : 0
	dict[messageKeys.ShowTimeline] = isDisplayEnabled(stored, 'ShowTimeline', false) ? 1 : 0
	dict[messageKeys.ShowTimelineLabels] = isDisplayEnabled(stored, 'ShowTimelineLabels', false) ? 1 : 0
	dict[messageKeys.SleepHoursX10] = parseSleepHoursX10(stored)
	dict[messageKeys.WakeFromSleep] = isDisplayEnabled(stored, 'WakeFromSleep') ? 1 : 0
	dict[messageKeys.ManualWakeMin] = parseTimeToMinutes(getClaySetting(stored, 'ManualWakeTime'), 420)
	dict[messageKeys.WakeDurationMin] = parsePositiveInt(getClaySetting(stored, 'WakeDurationMin'), 45)
	dict[messageKeys.WindDownMin] = parsePositiveInt(getClaySetting(stored, 'WindDownMin'), 60)
	dict[messageKeys.ShowTime] = isDisplayEnabled(stored, 'ShowTime') ? 1 : 0
	dict[messageKeys.ShowDate] = isDisplayEnabled(stored, 'ShowDate', false) ? 1 : 0
	dict[messageKeys.DateAboveTime] = isDisplayEnabled(stored, 'DateAboveTime', false) ? 1 : 0
	dict[messageKeys.DateFormat] = parseDateFormat(stored)
	dict[messageKeys.HumanEraYear] = isDisplayEnabled(stored, 'HumanEraYear') ? 1 : 0

	Pebble.sendAppMessage(dict, function() {
		console.log('Settings delivered to watch')
	}, function(e) {
		var msg = e && e.error ? e.error.message : 'unknown'
		console.log('Settings failed: ' + msg)
		if (retries < 12) {
			setTimeout(function() {
				sendSettingsToWatch(stored, retries + 1)
			}, 500)
		}
	})
}

function isValidAltitude(alt) {
	return alt !== null && alt !== undefined && isFinite(alt)
}

function altitudeFromCoords(coords) {
	if (!coords || !isValidAltitude(coords.altitude)) {
		return null
	}
	if (isFinite(coords.altitudeAccuracy) && coords.altitudeAccuracy > 200) {
		console.log('GPS altitude accuracy poor: ' + coords.altitudeAccuracy + 'm')
	}
	return Math.round(coords.altitude)
}

function sendCoordsToWatch(lat, lon, usePhoneGps, altitudeM, tzOffsetMin, retries) {
	retries = retries || 0

	if (lat === 0 && lon === 0) {
		console.log('Refusing to send zero coordinates')
		return
	}

	if (typeof Pebble.sendAppMessage !== 'function') {
		console.log('sendAppMessage unavailable')
		return
	}

	var dict = {}
	dict[messageKeys.UsePhoneGPS] = usePhoneGps ? 1 : 0
	dict[messageKeys.LatitudeE6] = Math.round(lat * 1000000)
	dict[messageKeys.LongitudeE6] = Math.round(lon * 1000000)
	dict[messageKeys.AltitudeM] = altitudeM > 0 ? altitudeM : 0
	dict[messageKeys.TimezoneOffsetM] = tzOffsetMin

	console.log('Sending coords: lat=' + lat + ' lon=' + lon + ' elev=' + (altitudeM || 0) + 'm tz=' + tzOffsetMin + ' gps=' + usePhoneGps)

	Pebble.sendAppMessage(dict, function() {
		console.log('Coordinates delivered to watch')
	}, function(e) {
		var msg = e && e.error ? e.error.message : 'unknown'
		console.log('Coordinates failed: ' + msg)
		if (retries < 12) {
			setTimeout(function() {
				sendCoordsToWatch(lat, lon, usePhoneGps, altitudeM, tzOffsetMin, retries + 1)
			}, 500)
		}
	})
}

function parseCoordinate(value) {
	if (value === undefined || value === null || value === '') {
		return 0
	}
	var parsed = parseFloat(value)
	return isNaN(parsed) ? 0 : parsed
}

function coordFromStored(stored, key) {
	return parseCoordinate(getClaySetting(stored, key))
}

function altitudeFromStored(stored) {
	return Math.round(parseCoordinate(getClaySetting(stored, 'Altitude')))
}

function deliverCoordsOnly(stored) {
	var useGps = isGpsEnabled(stored)
	var manualLat = coordFromStored(stored, 'Latitude')
	var manualLon = coordFromStored(stored, 'Longitude')
	var manualAlt = altitudeFromStored(stored)
	var tzOffsetMin = phoneTimezoneOffsetMin()

	if (!useGps) {
		if (manualLat === 0 && manualLon === 0) {
			console.log('No manual coordinates configured')
			return
		}
		sendCoordsToWatch(manualLat, manualLon, false, manualAlt, tzOffsetMin)
		return
	}

	if (!navigator.geolocation) {
		console.log('geolocation API unavailable')
		if (manualLat !== 0 || manualLon !== 0) {
			sendCoordsToWatch(manualLat, manualLon, true, cachedAltitudeM, tzOffsetMin)
		}
		return
	}

	navigator.geolocation.getCurrentPosition(
		function(pos) {
			var phoneAlt = altitudeFromCoords(pos.coords)
			var altitudeM = phoneAlt || cachedAltitudeM
			if (phoneAlt) {
				saveCachedAltitude(phoneAlt)
			}

			var lat = pos.coords.latitude
			var lon = pos.coords.longitude

			if (lat === 0 && lon === 0) {
				console.log('Refusing to send zero coordinates')
				return
			}

			var clayUpdate = {
				Latitude: formatCoord(lat),
				Longitude: formatCoord(lon)
			}
			if (phoneAlt) {
				clayUpdate.Altitude = String(phoneAlt)
			}
			clay.setSettings(clayUpdate)

			if (phoneAlt) {
				console.log('Phone altitude: ' + phoneAlt + 'm')
			} else {
				console.log('Phone altitude unavailable, using cached ' + (cachedAltitudeM || 0) + 'm')
			}

			sendCoordsToWatch(lat, lon, true, altitudeM, tzOffsetMin)
		},
		function(err) {
			console.log('GPS error ' + err.code + ': ' + err.message)
			if (manualLat !== 0 || manualLon !== 0) {
				sendCoordsToWatch(manualLat, manualLon, true, cachedAltitudeM, tzOffsetMin)
				return
			}
			console.log('GPS failed, no coordinates to send')
		},
		GEO_OPTIONS
	)
}

function deliverCoordsFromSettings(stored) {
	sendSettingsToWatch(stored)
	deliverCoordsOnly(stored)
}

function deliverCoords() {
	deliverCoordsOnly(readStoredClaySettings())
}

function isSolarRequest(payload) {
	if (!payload) {
		return false
	}
	if (payload[messageKeys.REQUEST_SOLAR]) {
		return true
	}
	if (payload.REQUEST_SOLAR) {
		return true
	}
	return false
}

function scheduleDeliveries() {
	var stored = readStoredClaySettings()
	sendSettingsToWatch(stored)
	deliverCoordsOnly(stored)
	setTimeout(function() {
		deliverCoordsOnly(readStoredClaySettings())
	}, 2000)
}

loadCachedAltitude()

Pebble.addEventListener('ready', function() {
	setTimeout(scheduleDeliveries, 300)
})

Pebble.addEventListener('showConfiguration', function() {
	Pebble.openURL(clay.generateUrl())
})

Pebble.addEventListener('webviewclosed', function(e) {
	if (!e || !e.response || e.response === 'CANCELLED') {
		return
	}

	var settings = clay.getSettings(e.response, false)
	deliverCoordsFromSettings(settings)
})

function isProfileAck(payload) {
	if (!payload) {
		return false
	}
	return payload[messageKeys.DbgProfileAck] !== undefined
		|| payload.DbgProfileAck !== undefined
}

function logProfileAck(payload) {
	var ack = payload[messageKeys.DbgProfileAck]
	if (ack === undefined) {
		ack = payload.DbgProfileAck
	}
	var elapsed = payload[messageKeys.DbgProfileElapsed]
	if (elapsed === undefined) {
		elapsed = payload.DbgProfileElapsed
	}
	var tick = payload[messageKeys.DbgProfileTick]
	if (tick === undefined) {
		tick = payload.DbgProfileTick
	}
	var label = ack === 1 ? 'reset' : ack === 2 ? 'dump' : 'cmd ' + ack
	console.log('[profile] ' + label + ' +'
		+ (elapsed || 0) + 's tick=' + (tick || 0)
		+ ' (see pebble logs for full counters)')
}

Pebble.addEventListener('appmessage', function(e) {
	if (isProfileAck(e.payload)) {
		logProfileAck(e.payload)
		return
	}

	if (!isSolarRequest(e.payload)) {
		return
	}

	deliverCoords()
})
