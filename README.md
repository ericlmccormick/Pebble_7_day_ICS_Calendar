# 7-Day Hybrid Calendar (Pebble Plugin + Optional ICS)

**Experimental sibling app** derived from 7-Day ICS Calendar v1.8. Maintained on the `feature/hybrid-calendar-plugin` branch, with a **different app UUID** so the two apps can be installed independently. Do not merge this branch into the existing ICS app's `main`: this is a separate application variant.

## Calendar sources

1. **Native Pebble Calendar**, enabled by default. With Plugins Preview enabled and Calendar access permitted, the companion subscribes to `calendar/event` and reads upcoming events from the calendars enabled in the Pebble mobile app (Settings → Calendar).
2. **ICS Calendars 1–3**, optional. Blank URLs are ignored. They continue to use the existing Google Apps Script ICS proxy. If all three URLs are blank, the app uses only the plugin. If the plugin is disabled, the app uses only ICS.

Both sources merge into one date-sorted agenda with the same watch UI, settings, App Glance, compact 65-byte event records, transactional Bluetooth acknowledgements, and offline event list as the parent v1.8 app.

**Duplicate policy is intentionally aggressive:** titles are compared case-insensitively after trimming/collapsing spaces, and **only the earliest appointment with a given title is retained**, even on a different day. For example, Monday "Gym" and Wednesday "Gym" become one item. Change this policy if you want recurring appointments preserved.

## Native plugin's actual limits

According to `libpebble3/.../plugin/CalendarPlugin.kt` in the official Pebble mobile app:
- Queries a **seven-day future window**, returning **up to 10 upcoming events total**, across all user-enabled calendars.
- Fields: `name`, `starts_at`, `ends_at`, `location`, `all_day`, `calendar`.
- `starts_at` and `ends_at` use the Pebble plugin's `timestamp` shape (epoch seconds).
- `calendar` is the display name of the originating calendar. The settings UI shows each discovered name with an individual color picker (Blue, Red, Green, Yellow, Orange, Purple), saved between app restarts. Calendars with no upcoming events will not be discovered until they have an event. Identically named calendars are treated as one color group.
- The built-in plugin refreshes on a timer and calendar-change signals while subscribed; no guarantee of background updates after the watch app exits.

The plugin does **not** guarantee all events within seven days; the maximum of 10 may omit later events.

## Settings

Open Pebble mobile app → 7day Hybrid Calendar → Settings. Enable/disable the native Calendar plugin and select a default color. When the plugin has supplied event data, individually discovered calendar names appear with separate color selectors. ICS 1–3 URLs remain optional, each with its own color. The watch's long-press Select menu controls Force Sync, Auto/Light/Dark appearance, font size, past-event filtering, time format, detail status bar, and last sync time.

The existing **Auto** appearance is based on the watch's local time: dark at 19:00–06:59, light at 07:00–18:59. It is **not** based on sunrise/sunset. A future implementation could read sunrise/sunset timestamps from `weather/location`.

## Requirements and testing

- The Pebble mobile app's **Plugins Preview** must be enabled for native calendar access. The app declares the `Calendar` permission. If the plugin is absent or permission is denied, configured ICS feeds can still provide data; if there are no usable sources, the watch preserves its last valid calendar.
- Plugin access and exact schema are experimental and may change. **Do not publish this build to the Pebble Appstore** until the plugin API becomes stable.
- `npm test` runs UTF-8, source/deduplication, transaction/retry, and plugin-only/mixed-source tests.
- `pebble build` must still be run in a Pebble SDK/CloudPebble environment. The current branch has not been compiled on a physical watch.
- The app is a branch-based independent variant; creating an entirely separate GitHub repository requires a repository-creation operation outside the available GitHub connector.

## Security

Treat private ICS feed URLs as credentials. ICS feeds are fetched via the configured Apps Script proxy. Native plugin data comes from the Pebble mobile app's configured calendars and is subject to user permission.

Source: https://github.com/coredevices/mobileapp/blob/main/libpebble3/src/commonMain/kotlin/io/rebble/libpebblecommon/plugin/CalendarPlugin.kt
