module.exports = function() {
	var clayConfig = this

	function syncManualFields() {
		var gpsToggle = clayConfig.getItemByMessageKey('UsePhoneGPS')
		var latInput = clayConfig.getItemByMessageKey('Latitude')
		var lonInput = clayConfig.getItemByMessageKey('Longitude')
		var altInput = clayConfig.getItemByMessageKey('Altitude')
		if (!gpsToggle || !latInput || !lonInput || !altInput) {
			return
		}

		if (gpsToggle.get()) {
			latInput.disable()
			lonInput.disable()
			altInput.disable()
		} else {
			latInput.enable()
			lonInput.enable()
			altInput.enable()
		}
	}

	function syncDateFields() {
		var showDate = clayConfig.getItemByMessageKey('ShowDate')
		var dateFormat = clayConfig.getItemByMessageKey('DateFormat')
		var humanEra = clayConfig.getItemByMessageKey('HumanEraYear')
		if (!showDate) {
			return
		}

		var enabled = showDate.get()
		if (dateFormat) {
			if (enabled) {
				dateFormat.enable()
			} else {
				dateFormat.disable()
			}
		}
		if (humanEra) {
			if (enabled) {
				humanEra.enable()
			} else {
				humanEra.disable()
			}
		}
	}

	clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function() {
		var gpsToggle = clayConfig.getItemByMessageKey('UsePhoneGPS')
		if (gpsToggle) {
			gpsToggle.on('change', syncManualFields)
			syncManualFields()
		}

		var showDate = clayConfig.getItemByMessageKey('ShowDate')
		if (showDate) {
			showDate.on('change', syncDateFields)
			syncDateFields()
		}
	})
}
