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
        "label": "Calendar 1 ICS URL",
        "attributes": {
          "placeholder": "https://calendar.google.com/...",
          "type": "url"
        }
      },
      {
        "type": "input",
        "messageKey": "ICAL_URL_2",
        "label": "Calendar 2 ICS URL (optional)",
        "attributes": {
          "placeholder": "https://calendar.google.com/...",
          "type": "url"
        }
      },
      {
        "type": "input",
        "messageKey": "ICAL_URL_3",
        "label": "Calendar 3 ICS URL (optional)",
        "attributes": {
          "placeholder": "https://calendar.google.com/...",
          "type": "url"
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