# 7-Day Hybrid Calendar — Experimental

Combine your Pebble phone's calendars with up to three optional ICS feeds in one color-coded, seven-day agenda.

**Native Calendar Plugin:** Enable the built-in Calendar source to read the next **10 upcoming appointments within seven days** from calendars enabled in the Pebble mobile app. Calendar names are discovered automatically, so you can assign individual colors in the phone settings.

**Optional ICS Calendars:** Paste up to three ICS URLs, each with its own color. Leave every URL blank to run entirely on the native Calendar plugin. Disable the plugin if you prefer ICS only.

**Duplicate Removal:** Events with the same title (ignoring case and surrounding whitespace) are reduced to one, even if the appointments occur at different times. The earliest occurrence is kept.

**Pebble Features:** Offline cached event list, App Glance on supported watches, scrollable details, Force Sync, font sizing, time format, themes, past-event filtering, and optional detail status bar.

**Theme Auto Mode:** Uses the watch's local clock, switching to dark at 7:00 PM and back to light at 7:00 AM. This is not a solar sunrise/sunset calculation.

**Important:** Plugins are still an experimental Pebble feature. The native plugin returns at most 10 events, even if more are scheduled within seven days. Additional future events require ICS feeds. Some event fields, such as descriptions, are available only from ICS, not from Pebble's current Calendar plugin. Extended event details depend on the phone.

**Requirements:** Enable Plugins Preview and allow Calendar access in the Pebble mobile app. Do not publish this plugin-dependent app until Pebble's API is stable.

Support development: https://ko-fi.com/ericmccormick
