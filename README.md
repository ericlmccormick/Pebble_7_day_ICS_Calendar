# 7-Day ICS Calendar for Pebble

A compact seven-day agenda for Pebble watches, using up to three ICS feeds. Calendar titles, times, and color markers are stored on the watch for offline access.

## Features (v1.8 development)

- **Up to three ICS feeds** combined chronologically, with six color choices per calendar
- **App Glance** showing the next event on supported Pebble devices
- **On-watch Settings:** long-press **Select** from the event list to open native settings
- **Force Sync**, automatic day/night theme, Light and Dark themes
- **System/Small/Medium/Large font sizes** and optional display of completed events
- **System/12-hour/24-hour time format** and optional native status bar in detail view
- **Last Updated** timestamp in watch settings
- Scrollable event details including location, description, organizer and link text when the feed provides them
- Transaction-ID Bluetooth transfer with per-chunk acknowledgement, checksum verification, bounded retries and transactional storage
- UTF-8 text handling and round/monochrome layout support

## Setup

On your phone, open Pebble app → 7-Day ICS Calendar → Settings. Paste an ICS URL into **Calendar 1 ICS URL**. You can add two more feeds and choose colors. Save. For Google Calendar use **Settings and sharing → Integrate calendar → Secret address in iCal format**, where available. Treat secret ICS links as passwords.

The phone companion retrieves ICS data through the configured Google Apps Script proxy and sends compact event data to the watch. The proxy must be publicly callable (without requiring Google sign-in) and should be hosted and controlled by an entity you trust.

**Offline limitation:** After a successful sync, event titles/times/colors are cached on the watch. Extended descriptions, locations, and links are retained in phone storage, so they may be unavailable when disconnected. Metadata sent to the watch is limited by Bluetooth payload size and can be truncated.

## Controls

- **Up/Down**: Scroll the calendar or event details
- **Select**: Open an event
- **Long-press Select in the event list**: Open on-watch Settings
- **Settings → Force Sync**: Fetch fresh ICS data even if cached data is recent
- **Back**: Return to the event list

Automatic appearance changes to dark after 7 PM and back to light at 7 AM, based on the watch's local time. PebbleOS does not expose a universal light/dark system theme preference.

## Supported targets

Aplite, Basalt, Chalk, Diorite, Emery. Newer Flint and Gabbro targets are not yet enabled because of the current Clay dependency. App Glance is not available on Aplite.

## Development

Run `npm test` for companion regression tests and `pebble build` using a compatible Pebble SDK or CloudPebble to verify the C/watch implementation. Source in `main` is the stable release; v1.8 changes live in `feature/v1.8-watch-settings-and-sync` until verified.

If you'd like to support development: https://ko-fi.com/ericmccormick
