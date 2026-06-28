module.exports = [
	{
		type: 'section',
		items: [
			{
				type: 'toggle',
				messageKey: 'ShowLabels',
				label: 'Add labels for extra distraction',
				defaultValue: true
			}
		]
	},
	{
		type: 'heading',
		defaultValue: 'Display'
	},
	{
		type: 'section',
		items: [
			{
				type: 'toggle',
				messageKey: 'ShowTime',
				label: 'Show time (why not... ¯\\_(ツ)_/¯)',
				defaultValue: true
			},
			{
				type: 'toggle',
				messageKey: 'ShowSteps',
				label: 'Show steps',
				defaultValue: false
			},
			{
				type: 'toggle',
				messageKey: 'ShowBpm',
				label: 'Show BPM',
				defaultValue: false
			},
			{
				type: 'toggle',
				messageKey: 'ShowBattery',
				label: 'Show battery',
				defaultValue: true
			},
			{
				type: 'toggle',
				messageKey: 'ShowDate',
				label: 'Show date',
				defaultValue: false
			},
			{
				type: 'toggle',
				messageKey: 'DateAboveTime',
				label: 'Swap date and timeline positions',
				defaultValue: true
			},
			{
				type: 'select',
				messageKey: 'DateFormat',
				label: 'Date format',
				defaultValue: '0',
				options: [
					{ label: 'TUESDAY', value: '0' },
					{ label: 'TUESDAY 16 JUNE', value: '1' },
					{ label: 'TUE 16 JUNE', value: '2' },
					{ label: 'TUE 16 JUN 2026', value: '3' },
					{ label: '16 JUN 2026', value: '4' },
					{ label: '16 JUNE', value: '5' },
					{ label: '2026/06/16 TUE', value: '6' },
					{ label: '2026/06/16', value: '7' },
					{ label: '2026 06 16 TUE', value: '8' },
					{ label: '2026 06 16', value: '9' },
					{ label: 'Always FRIDAY', value: '10' }
				]
			},
			{
				type: 'toggle',
				messageKey: 'HumanEraYear',
				label: 'Human Era year (+10K)',
				defaultValue: false
			}
		]
	},
	{
		type: 'heading',
		defaultValue: 'Sun & moon'
	},
	{
		type: 'section',
		items: [
			{
				type: 'toggle',
				messageKey: 'ShowSun',
				label: 'Show sun times',
				defaultValue: false
			},
			{
				type: 'toggle',
				messageKey: 'ShowMoon',
				label: 'Show moon times',
				defaultValue: false
			},
			{
				type: 'toggle',
				messageKey: 'UsePhoneGPS',
				label: 'Use phone location',
				defaultValue: false
			},
			{
				type: 'input',
				messageKey: 'Latitude',
				label: 'Latitude',
				defaultValue: '',
				attributes: {
					type: 'text',
					inputmode: 'decimal',
					placeholder: '55.7558'
				}
			},
			{
				type: 'input',
				messageKey: 'Longitude',
				label: 'Longitude',
				defaultValue: '',
				attributes: {
					type: 'text',
					inputmode: 'decimal',
					placeholder: '37.6173'
				}
			},
			{
				type: 'input',
				messageKey: 'Altitude',
				label: 'Elevation (m)',
				defaultValue: '',
				attributes: {
					type: 'number',
					placeholder: '0'
				}
			}
		]
	},
	{
		type: 'heading',
		defaultValue: 'Timeline'
	},
	{
		type: 'section',
		items: [
			{
				type: 'toggle',
				messageKey: 'ShowTimeline',
				label: 'Show timeline',
				defaultValue: false
			},
			{
				type: 'toggle',
				messageKey: 'ShowTimelineLabels',
				label: 'Show wake & bed times',
				defaultValue: false
			},
			{
				type: 'input',
				messageKey: 'SleepHours',
				label: 'Sleep (hours)',
				defaultValue: '8',
				attributes: {
					type: 'number',
					step: '0.5',
					min: '1',
					max: '16',
					placeholder: '8'
				}
			},
			{
				type: 'input',
				messageKey: 'ManualWakeTime',
				label: 'Default wake time',
				defaultValue: '07:00',
				attributes: {
					type: 'time'
				}
			},
			{
				type: 'text',
				defaultValue: 'Always used when sleep data is off or unavailable.'
			},
			{
				type: 'toggle',
				messageKey: 'WakeFromSleep',
				label: 'Prefer wake time from sleep data',
				defaultValue: true
			},
			{
				type: 'input',
				messageKey: 'WakeDurationMin',
				label: 'Wake-up routine aka Morning Zombie mode (minutes)',
				defaultValue: '45',
				attributes: {
					type: 'number',
					min: '0',
					max: '240',
					placeholder: '45'
				}
			},
			{
				type: 'input',
				messageKey: 'WindDownMin',
				label: 'Wind-down before bed (minutes)',
				defaultValue: '60',
				attributes: {
					type: 'number',
					min: '0',
					max: '240',
					placeholder: '60'
				}
			}
		]
	},
	{
		type: 'section',
		items: [
			{
				type: 'toggle',
				label: 'For those who want to confuse themselves with 12-hour AM/PM badly enough to toggle the switch that does absolutely nothing :P',
				defaultValue: false
			}
		]
	},
	{
		type: 'submit',
		defaultValue: 'Save'
	}
]
