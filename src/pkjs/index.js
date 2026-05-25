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
var CHUNK_SIZE = 4;
// How many bytes a single CalendarEvent struct takes up in C (8 bytes for time + 56 bytes for title = 64)
var BYTES_PER_EVENT = 64; 
// How many minutes the phone will wait before bothering Google for a fresh update
var CACHE_MINUTES = 30;
// The URL of your deployed Google Apps Script (The Proxy)
var PROXY_URL = 'https://script.google.com/macros/s/AKfycby4NxpqCm9IYW3ijM_8UI1S95dNquYAF2wYm9sGxxveTrEcZ1yL5Gu5KUcDbMS5OoYB/exec';

// --- Core Functions ---

/**
 * Reaches out to the Google Apps Script to get the latest calendar events.
 * @param {string} url - The user's private Google Calendar iCal URL
 */
function fetchIcal(url) {
  console.log('JS: Fetching fresh data from Google...');
  
  // Create a standard AJAX HTTP request
  var req = new XMLHttpRequest();
  // Open a GET request to the Proxy, attaching the user's iCal URL safely encoded
  req.open('GET', PROXY_URL + '?url=' + encodeURIComponent(url), true);
  
  // Define what happens when the request finishes
  req.onreadystatechange = function() {
    // readyState 4 means "Done", status 200 means "Success"
    if (req.readyState === 4 && req.status === 200) {
      try {
        // Convert the plain text response from Google into a Javascript Array of objects
        var events = JSON.parse(req.responseText);
        
        // If Google found events in the next 7 days, start sending them to the watch
        if (events.length > 0) {
          sendEventByteArray(events, 0);
        } else {
          console.log('JS: Google returned zero events for the next 7 days.');
        }
      } catch(e) { 
        console.log('JS: JSON Parse Error. Google sent bad data.'); 
      }
    }
  };
  // Send the request
  req.send(null);
}

/**
 * Converts JS objects into raw bytes and sends them to the watch in manageable chunks.
 * This function calls itself recursively until all events are sent.
 * @param {Array} events - The full array of parsed calendar events
 * @param {number} start - The index of the array to start this specific chunk at
 */
function sendEventByteArray(events, start) {
  // Determine how many events to send in this chunk. 
  // It's usually CHUNK_SIZE (4), unless we are at the very end of the list.
  var count = Math.min(events.length - start, CHUNK_SIZE);
  
  // If we've run out of events, stop.
  if (count <= 0) return;
  
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
    
    // Write the title character by character (Max 55 chars to leave room for the null terminator)
    for (var c = 0; c < 55; c++) {
      // If the character exists in the string, write its char code. Otherwise, write 0 (null padding).
      bytes[offset + 8 + c] = (c < t.length) ? t.charCodeAt(c) : 0;
    }
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
    } else {
      // If there are more events, call this function again, shifting the starting index forward
      sendEventByteArray(events, start + CHUNK_SIZE);
    }
  }, function(err) {
    // --- ERROR CALLBACK ---
    console.log('JS: Failed to send chunk to watch. Bluetooth might be busy.');
    // If a chunk fails, the app will try again on the next launch rather than infinitely looping.
  });
}

// --- App Event Listeners ---

// Fired the moment the app boots up on the watch
Pebble.addEventListener('ready', function() {
  console.log('JS: PebbleKit JS Bridge Initialized.');
  
  // Retrieve saved data from the phone's local storage
  var savedUrl = localStorage.getItem('user_ical_url');
  var lastFetch = localStorage.getItem('last_fetch_time');
  var now = Date.now();

  // If the user has configured an iCal URL...
  if (savedUrl) {
    // Check if the cache is missing, OR if the current time minus the last fetch time is greater than 30 minutes
    if (!lastFetch || (now - parseInt(lastFetch)) > (CACHE_MINUTES * 60000)) {
      console.log('JS: Cache expired. Initiating background sync...');
      fetchIcal(savedUrl);
    } else {
      console.log('JS: Data is fresh. Watch will use its own internal storage.');
    }
  }
});

// Fired when the watch specifically asks the phone for help (The "Help Signal")
// This happens if the watch's internal storage gets wiped and it needs data IMMEDIATELY
Pebble.addEventListener('appmessage', function(e) {
  console.log('JS: Watch requested an emergency data sync!');
  var savedUrl = localStorage.getItem('user_ical_url');
  if (savedUrl) fetchIcal(savedUrl); // Bypass the 30-minute timer and fetch
});

// Fired when the user taps the "Settings" gear icon in the Pebble app on their phone
Pebble.addEventListener('showConfiguration', function() { 
  // Generate and open the Clay settings HTML page
  Pebble.openURL(clay.generateUrl()); 
});

// Fired when the user taps "Save" on the Clay settings page
Pebble.addEventListener('webviewclosed', function(e) {
  if (e && e.response) {
    // Convert the Clay response back into a standard dictionary
    var dict = clay.getSettings(e.response);
    
    // 1. Send the dictionary to the watch immediately (This updates the 12h/24h setting)
    Pebble.sendAppMessage(dict, function() {
      console.log('JS: Format settings pushed to watch.');
      
      // 2. Extract the URL using the numerical message key ID
      var url = dict[messageKeys.ICAL_URL];
      
      if (url) {
        console.log('JS: New URL detected in settings. Forcing data sync...');
        // Save the URL to local storage so the 'ready' event can find it next time
        localStorage.setItem('user_ical_url', url);
        // Force an immediate fetch from Google, bypassing the 30-minute timer
        fetchIcal(url);
      }
    });
  }
});