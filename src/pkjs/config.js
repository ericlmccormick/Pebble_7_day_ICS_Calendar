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
        "defaultValue": "12h",
        "label": "Time Format",
        "options": [
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
