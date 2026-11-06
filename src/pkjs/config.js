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
        "defaultValue": "Data Sources"
      },
      {
        "type": "heading",
        "defaultValue": "Native Pebble Calendar (Experimental)"
      },
      {
        "type": "select",
        "messageKey": "PLUGIN_ENABLED",
        "label": "Use calendars provided by Pebble's native Calendar plugin",
        "defaultValue": "true",
        "options": [
          {"label": "Enabled", "value": "true"},
          {"label": "Disabled (ICS only)", "value": "false"}
        ]
      },
      {
        "type": "select",
        "messageKey": "PLUGIN_COLOR",
        "label": "Native Calendar event color",
        "defaultValue": "4",
        "options": [
          {"label": "Blue", "value": "1"},
          {"label": "Red", "value": "2"},
          {"label": "Green", "value": "3"},
          {"label": "Yellow", "value": "4"},
          {"label": "Orange", "value": "5"},
          {"label": "Purple", "value": "6"}
        ]
      },
      {
        "type": "heading",
        "defaultValue": "Optional ICS Calendars (leave blank for plugin-only mode)"
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
        "type": "select",
        "messageKey": "ICAL_COLOR_1",
        "label": "Calendar 1 color",
        "defaultValue": "1",
        "options": [
          {
            "label": "Blue",
            "value": "1"
          },
          {
            "label": "Red",
            "value": "2"
          },
          {
            "label": "Green",
            "value": "3"
          },
          {
            "label": "Yellow",
            "value": "4"
          },
          {
            "label": "Orange",
            "value": "5"
          },
          {
            "label": "Purple",
            "value": "6"
          }
        ]
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
        "type": "select",
        "messageKey": "ICAL_COLOR_2",
        "label": "Calendar 2 color",
        "defaultValue": "2",
        "options": [
          {
            "label": "Blue",
            "value": "1"
          },
          {
            "label": "Red",
            "value": "2"
          },
          {
            "label": "Green",
            "value": "3"
          },
          {
            "label": "Yellow",
            "value": "4"
          },
          {
            "label": "Orange",
            "value": "5"
          },
          {
            "label": "Purple",
            "value": "6"
          }
        ]
      },
      {
        "type": "input",
        "messageKey": "ICAL_URL_3",
        "label": "Calendar 3 ICS URL (optional)",
        "attributes": {
          "placeholder": "https://calendar.google.com/...",
          "type": "url"
        }
      },
      {
        "type": "select",
        "messageKey": "ICAL_COLOR_3",
        "label": "Calendar 3 color",
        "defaultValue": "3",
        "options": [
          {
            "label": "Blue",
            "value": "1"
          },
          {
            "label": "Red",
            "value": "2"
          },
          {
            "label": "Green",
            "value": "3"
          },
          {
            "label": "Yellow",
            "value": "4"
          },
          {
            "label": "Orange",
            "value": "5"
          },
          {
            "label": "Purple",
            "value": "6"
          }
        ]
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
        "messageKey": "THEME_MODE",
        "defaultValue": "0",
        "label": "Appearance",
        "options": [
          {"label": "Auto (day/night)", "value": "0"},
          {"label": "Light", "value": "1"},
          {"label": "Dark", "value": "2"}
        ]
      },
      {
        "type": "select",
        "messageKey": "SHOW_PAST",
        "defaultValue": "0",
        "label": "Past Events",
        "options": [
          {"label": "Hide completed events", "value": "0"},
          {"label": "Show all events", "value": "1"}
        ]
      },
      {
        "type": "select",
        "messageKey": "DETAIL_STATUS_BAR",
        "defaultValue": "0",
        "label": "Detail Status Bar",
        "options": [
          {"label": "Off (more reading space)", "value": "0"},
          {"label": "On (show current time)", "value": "1"}
        ]
      },
      {
        "type": "select",
        "messageKey": "FONT_SIZE",
        "defaultValue": "0",
        "label": "Event Font Size",
        "options": [
          { "label": "System Default", "value": "0" },
          { "label": "Small", "value": "1" },
          { "label": "Medium", "value": "2" },
          { "label": "Large", "value": "3" }
        ]
      },
      {
        "type": "select",
        "messageKey": "TIME_FORMAT",
        "defaultValue": "System",
        "label": "Time Format",
        "options": [
          {
            "label": "System Watch Preference",
            "value": "System"
          },
          {
            "label": "12 Hour (AM/PM)",
            "value": "12h"
          },
          {
            "label": "24 Hour",
            "value": "24h"
          }
        ]
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
