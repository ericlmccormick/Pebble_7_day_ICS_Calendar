// --- Imports ---
// Require the Pebble Clay library, which builds the settings page automatically
var Clay = require('pebble-clay');
// Require the configuration file that tells Clay what fields to draw (URL, 12h/24h)
var clayConfig = require('./config');
// Require the message_keys dictionary so we can map names (like 'ICAL_URL') to their numerical IDs
var messageKeys = require('message_keys');

// Initialize Clay. 'autoHandleEvents: false' tells Clay that we want to manually 
// handle what happens when the user clicks "Save", rather than letting Clay do it blindly.
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

// --- Configuration Constants ---
// How many events we send to the watch in a single Bluetooth packet
var CHUNK_SIZE = 2;
var MAX_EVENTS = 48;
var syncBusy = false;
var pendingRefresh = false;
// How many bytes a single CalendarEvent struct takes up in C (8 bytes for time + 56 bytes for title = 64)
var BYTES_PER_EVENT = 65; 
// How many minutes the phone will wait before bothering Google for a fresh update
var CACHE_MINUTES = 30;
// The URL of your deployed Google Apps Script (The Proxy)
var PROXY_URL = 'https://script.google.com/macros/s/AKfycby4NxpqCm9IYW3ijM_8UI1S95dNquYAF2wYm9sGxxveTrEcZ1yL5Gu5KUcDbMS5OoYB/exec';

// Encode titles as UTF-8 without splitting a multi-byte character.
// The C CalendarEvent struct reserves 56 bytes for its null-terminated title.
function encodeTitleUtf8(title, bytes, startOffset) {
  var used = 0;
  var limit = 55;

  for (var i = 0; i < title.length && used < limit; i++) {
    var cp = title.charCodeAt(i);

    // Decode UTF-16 surrogate pairs; replace malformed pairs.
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      var next = (i + 1 < title.length) ? title.charCodeAt(i + 1) : 0;
      if (next >= 0xDC00 && next <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (next - 0xDC00);
        i++;
      } else {
        cp = 0xFFFD;
      }
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
      cp = 0xFFFD;
    }

    var length = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
    if (used + length > limit) break;

    if (length === 1) {
      bytes[startOffset + used++] = cp;
    } else if (length === 2) {
      bytes[startOffset + used++] = 0xC0 | (cp >> 6);
      bytes[startOffset + used++] = 0x80 | (cp & 0x3F);
    } else if (length === 3) {
      bytes[startOffset + used++] = 0xE0 | (cp >> 12);
      bytes[startOffset + used++] = 0x80 | ((cp >> 6) & 0x3F);
      bytes[startOffset + used++] = 0x80 | (cp & 0x3F);
    } else {
      bytes[startOffset + used++] = 0xF0 | (cp >> 18);
      bytes[startOffset + used++] = 0x80 | ((cp >> 12) & 0x3F);
      bytes[startOffset + used++] = 0x80 | ((cp >> 6) & 0x3F);
      bytes[startOffset + used++] = 0x80 | (cp & 0x3F);
    }
  }

  // ArrayBuffer is initialized to zero, so the remaining bytes and terminator
  // are already null padded.
}

// --- Core Functions ---

// Preserve existing installations that stored just one calendar URL.
function getCalendarSources() {
  var sources = [];
  for (var i = 1; i <= 3; i++) {
    var value = localStorage.getItem('user_ical_url_' + i);
    if (i === 1 && value === null) value = localStorage.getItem('user_ical_url');
    value = (value || '').trim();
    var color = parseInt(localStorage.getItem('user_ical_color_' + i), 10);
    if (color < 1 || color > 6 || !isFinite(color)) color = i;
    if (value && !sources.some(function(source) { return source.url === value; })) {
      sources.push({url:value, color:color});
    }
  }
  return sources;
}

function fetchOneCalendar(url, callback) {
  var req = new XMLHttpRequest();
  var finished = false;
  function finish(err, events) {
    if (finished) return;
    finished = true;
    callback(err, events);
  }
  req.open('GET', PROXY_URL + '?url=' + encodeURIComponent(url), true);
  req.timeout = 15000;
  req.onreadystatechange = function() {
    if (req.readyState !== 4 || finished) return;
    if (req.status !== 200) return finish('HTTP ' + req.status);
    try {
      var data = JSON.parse(req.responseText);
      if (!Array.isArray(data)) throw new Error('Response is not an array');
      var valid = [];
      for (var i = 0; i < data.length; i++) {
        var ev = data[i];
        if (ev && isFinite(Number(ev.timestampMs)) &&
            Number(ev.timestampMs) > 0 &&
            (ev.endTimestampMs === undefined || isFinite(Number(ev.endTimestampMs)))) {
          valid.push(ev);
        }
      }
      finish(null, valid);
    } catch (e) {
      finish('Invalid response: ' + e.message);
    }
  };
  req.onerror = function() { finish('Network error'); };
  req.ontimeout = function() { finish('Request timeout'); };
  try { req.send(null); } catch (e) { finish('Request failed'); }
}

// Fetch up to three feeds sequentially to limit load on phones and the proxy.
// Commit a new combined calendar only when ALL configured feeds succeed.
function fetchIcal() {
  if (syncBusy) { pendingRefresh = true; return; }
  var urls = getCalendarSources();
  if (!urls.length) return;
  syncBusy = true;
  var all = [];
  var index = 0;
  function finishSync(success) {
    if (!success) {
      Pebble.sendAppMessage({SYNC_STATUS: 'error'}, function(){}, function(){});
    }
    syncBusy = false;
    if (pendingRefresh) {
      pendingRefresh = false;
      fetchIcal();
    }
  }
  function next() {
    if (index >= urls.length) {
      all.sort(function(a, b) { return Number(a.timestampMs) - Number(b.timestampMs); });
      var watchInfo = Pebble.getActiveWatchInfo ? Pebble.getActiveWatchInfo() : null;
      var limit = (watchInfo && watchInfo.platform === 'aplite') ? 20 : MAX_EVENTS;
      sendEventByteArray(all.slice(0, limit), 0, finishSync);
      return;
    }
    var feedNumber = ++index;
    var source = urls[feedNumber - 1];
    fetchOneCalendar(source.url, function(err, events) {
      if (err) {
        console.log('JS: Calendar ' + feedNumber + ' failed: ' + err);
        finishSync(false);
        return;
      }
      for (var i = 0; i < events.length; i++) {
        events[i].calendarColor = source.color;
        all.push(events[i]);
      }
      next();
    });
  }
  next();
}

/**
 * Converts JS objects into raw bytes and sends them to the watch in manageable chunks.
 * This function calls itself recursively until all events are sent.
 * @param {Array} events - The full array of parsed calendar events
 * @param {number} start - The index of the array to start this specific chunk at
 */
function sendEventByteArray(events, start, done) {
  // Determine how many events to send in this chunk. 
  // It's usually CHUNK_SIZE (4), unless we are at the very end of the list.
  var count = Math.min(events.length - start, CHUNK_SIZE);
  
  // If we've run out of events, stop.
  if (count <= 0 && start !== 0) { done(true); return; }
  
  // Create an empty chunk of memory exactly the right size for these events
  var buf = new ArrayBuffer(count * BYTES_PER_EVENT);
  // Create a DataView to write 32-bit integers (for timestamps)
  var view = new DataView(buf);
  // Create a Uint8Array to write individual 8-bit characters (for titles)
  var bytes = new Uint8Array(buf);
  
  // Loop through the events for this specific chunk
  for (var i = 0; i < count; i++) {
    // Calculate exactly where in the memory buffer this event starts
    var offset = i * BYTES_PER_EVENT;
    // Grab the event object from the main array
    var ev = events[start + i];
    
    // Write the start timestamp (Convert Javascript Milliseconds to Unix Seconds)
    view.setUint32(offset, Math.floor(ev.timestampMs / 1000), true);
    // Write the end timestamp
    view.setUint32(offset + 4, Math.floor((ev.endTimestampMs || ev.timestampMs) / 1000), true);
    
    // Grab the title, or use a fallback if it's missing
    var t = ev.title || "No Title";
    
    // Encode at most 55 UTF-8 bytes, leaving a null terminator.
    encodeTitleUtf8(t, bytes, offset + 8);
    bytes[offset + 64] = ev.calendarColor || 1;
  }
  
  // Convert the Uint8Array into a standard Javascript Array of numbers so Pebble can send it
  var arr = []; 
  for (var j = 0; j < bytes.length; j++) arr.push(bytes[j]);
  
  // Package the data into a dictionary and send it to the watch
  Pebble.sendAppMessage({
    "TOTAL_EVENTS": events.length,                   // Let the watch know how big the total list is
    "CHUNK_INDEX": Math.floor(start / CHUNK_SIZE),   // Let the watch know which piece of the puzzle this is (0, 1, 2...)
    "EVENT_DATA": arr                                // The raw bytes
  }, function() {
    // --- SUCCESS CALLBACK ---
    // If the watch successfully received the chunk...
    
    // Check if we just sent the very last piece of data
    if (start + CHUNK_SIZE >= events.length) {
      console.log('JS: All chunks sent successfully. Sync complete.');
      // ONLY NOW do we update the timestamp. This prevents partial syncs from breaking the 30-minute rule.
      localStorage.setItem('last_fetch_time', Date.now().toString());
      done(true);
    } else {
      // If there are more events, call this function again, shifting the starting index forward
      sendEventByteArray(events, start + CHUNK_SIZE, done);
    }
  }, function(err) {
    // --- ERROR CALLBACK ---
    console.log('JS: Failed to send chunk to watch. Bluetooth might be busy.');
    done(false);
    // If a chunk fails, the app will try again on the next launch rather than infinitely looping.
  });
}

// --- App Event Listeners ---

// Fired the moment the app boots up on the watch
Pebble.addEventListener('ready', function() {
  console.log('JS: PebbleKit JS Bridge Initialized.');
  
  // Retrieve saved data from the phone's local storage
  var hasCalendars = getCalendarSources().length > 0;
  var lastFetch = localStorage.getItem('last_fetch_time');
  var now = Date.now();

  // If the user has configured an iCal URL...
  if (hasCalendars) {
    // Check if the cache is missing, OR if the current time minus the last fetch time is greater than 30 minutes
    if (!lastFetch || (now - parseInt(lastFetch)) > (CACHE_MINUTES * 60000)) {
      console.log('JS: Cache expired. Initiating background sync...');
      fetchIcal();
    } else {
      console.log('JS: Data is fresh. Watch will use its own internal storage.');
    }
  }
});

// Fired when the watch specifically asks the phone for help (The "Help Signal")
// This happens if the watch's internal storage gets wiped and it needs data IMMEDIATELY
Pebble.addEventListener('appmessage', function(e) {
  console.log('JS: Watch requested an emergency data sync!');
  if (getCalendarSources().length) fetchIcal(); // Force all configured calendars
});

// Fired when the user taps the "Settings" gear icon in the Pebble app on their phone
Pebble.addEventListener('showConfiguration', function() { 
  // Generate and open the Clay settings HTML page
  Pebble.openURL(clay.generateUrl()); 
});

// Store all three URLs locally; only send the time preference to the watch.
Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response || e.response === 'CANCELLED') return;
  var dict;
  try { dict = clay.getSettings(e.response); }
  catch (err) { console.log('JS: Invalid settings response'); return; }

  var keys = [messageKeys.ICAL_URL, messageKeys.ICAL_URL_2, messageKeys.ICAL_URL_3];
  for (var i = 0; i < keys.length; i++) {
    var value = dict[keys[i]];
    if (value !== undefined && value !== null) {
      localStorage.setItem('user_ical_url_' + (i + 1), String(value).trim());
    }
  }
  for (var c = 1; c <= 3; c++) {
    var selectedColor = parseInt(dict[messageKeys['ICAL_COLOR_' + c]], 10);
    if (selectedColor >= 1 && selectedColor <= 6) {
      localStorage.setItem('user_ical_color_' + c, String(selectedColor));
    }
  }
  // Avoid sending long, private ICS URLs through AppMessage.
  if (dict[messageKeys.TIME_FORMAT] !== undefined) {
    Pebble.sendAppMessage({TIME_FORMAT: dict[messageKeys.TIME_FORMAT]},
      function(){}, function(){ console.log('JS: Time format update failed'); });
  }
  localStorage.removeItem('last_fetch_time');
  fetchIcal();
});
