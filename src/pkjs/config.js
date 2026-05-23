module.exports = [
  {
    "type": "heading",
    "defaultValue": "Calendar Settings"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Data Source"
      },
      {
        "type": "input",
        "messageKey": "ICAL_URL",
        "label": "Google Calendar iCal URL",
        "attributes": {
          "placeholder": "https://calendar.google.com/...",
          "type": "url"
        }
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Preferences"
      },
      {
        "type": "select",
        "messageKey": "TIME_FORMAT",
        "defaultValue": "12h",
        "label": "Time Format",
        "options": [
          { "label": "12 Hour (AM/PM)", "value": "12h" },
          { "label": "24 Hour", "value": "24h" }
        ]
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];