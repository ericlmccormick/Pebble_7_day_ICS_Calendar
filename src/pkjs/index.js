var Clay = require('pebble-clay');
var clayConfig = require('./config');
var messageKeys = require('message_keys');
var clay = new Clay(clayConfig, null, { autoHandleEvents: false });

var CHUNK_SIZE = 4;
var BYTES_PER_EVENT = 64; 
var CACHE_MINUTES = 30;
var PROXY_URL = 'https://script.google.com/macros/s/AKfycbxU7e1TPoiMk2iBg9wITis5Mw_z0rCSkfEO-t8wVSPNcX9OvRXy6hcCNvYrmgtAY6NClw/exec';

function fetchIcal(url) {
  console.log('JS: Fetching fresh data from Google...');
  var req = new XMLHttpRequest();
  req.open('GET', PROXY_URL + '?url=' + encodeURIComponent(url), true);
  req.onreadystatechange = function() {
    if (req.readyState === 4 && req.status === 200) {
      try {
        var events = JSON.parse(req.responseText);
        if (events.length > 0) {
          sendEventByteArray(events, 0);
        }
      } catch(e) { console.log('JS: JSON Parse Error'); }
    }
  };
  req.send(null);
}

function sendEventByteArray(events, start) {
  var count = Math.min(events.length - start, CHUNK_SIZE);
  if (count <= 0) return;
  var buf = new ArrayBuffer(count * BYTES_PER_EVENT);
  var view = new DataView(buf);
  var bytes = new Uint8Array(buf);
  for (var i = 0; i < count; i++) {
    var offset = i * BYTES_PER_EVENT;
    var ev = events[start + i];
    view.setUint32(offset, Math.floor(ev.timestampMs / 1000), true);
    view.setUint32(offset + 4, Math.floor((ev.endTimestampMs || ev.timestampMs) / 1000), true);
    var t = ev.title || "No Title";
    for (var c = 0; c < 55; c++) bytes[offset + 8 + c] = (c < t.length) ? t.charCodeAt(c) : 0;
  }
  var arr = []; for (var j = 0; j < bytes.length; j++) arr.push(bytes[j]);
  Pebble.sendAppMessage({
    "TOTAL_EVENTS": events.length, "CHUNK_INDEX": Math.floor(start / CHUNK_SIZE), "EVENT_DATA": arr
  }, function() {
    if (start + CHUNK_SIZE >= events.length) {
      console.log('JS: Sync complete.');
      localStorage.setItem('last_fetch_time', Date.now().toString());
    } else {
      sendEventByteArray(events, start + CHUNK_SIZE);
    }
  });
}

Pebble.addEventListener('ready', function() {
  var savedUrl = localStorage.getItem('user_ical_url');
  var lastFetch = localStorage.getItem('last_fetch_time');
  var now = Date.now();

  if (savedUrl && (!lastFetch || (now - parseInt(lastFetch)) > (CACHE_MINUTES * 60000))) {
    fetchIcal(savedUrl);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  var savedUrl = localStorage.getItem('user_ical_url');
  if (savedUrl) fetchIcal(savedUrl);
});

Pebble.addEventListener('showConfiguration', function() { 
  Pebble.openURL(clay.generateUrl()); 
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && e.response) {
    var dict = clay.getSettings(e.response);
    Pebble.sendAppMessage(dict, function() {
      var url = dict[messageKeys.ICAL_URL];
      if (url) {
        localStorage.setItem('user_ical_url', url);
        fetchIcal(url);
      }
    });
  }
});