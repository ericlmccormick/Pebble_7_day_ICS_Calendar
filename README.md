# 7day ICS Calendar

**Current source version:** 1.7.0. A configurable seven-day ICS calendar for Pebble watches. This is independent of [Next Up Agenda](https://github.com/ericlmccormick/Next-Up-Agenda), which gets events from the native Pebble Calendar plugin instead of ICS URLs.

## Setup

Open the app's settings in the Pebble phone app and enter up to **three** ICS calendar URLs. Assign a color to each calendar and choose the event font size and 12/24-hour time format. A publicly reachable HTTPS ICS URL or a suitable feed/proxy may be necessary depending on your calendar provider. Keep private calendar URLs secret: some contain access tokens.

The app sends calendar data to the watch and supports event details. The seven-day scope, supported event fields and recurrence expansion depend on the feed and any upstream ICS processing. Calendar updates require phone network access.

## Testing and building

Run `npm test` for UTF-8 and multiple-calendar checks; additional all-day tests are in `tests/`. The [PBW workflow](.github/workflows/direct-pbw.yml) builds the native Pebble app. `package.json` is authoritative for the release number and supported platforms (Aplite, Basalt, Chalk, Diorite, Emery).

## Support

When reporting a feed parsing bug, redact sensitive URLs and private event information. https://ko-fi.com/ericmccormick
