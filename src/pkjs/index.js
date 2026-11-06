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
var activeTransfer = null;
var transferSequence = 0;
var detailCacheRestored = false;
var lastWatchTransferAt = 0;
// Duplicate sync triggers during an active fetch are ignored.
// Pebble 'ready' and the watch's initial sync ping often overlap.
// How many bytes a single CalendarEvent struct takes up in C (8 bytes for time + 56 bytes for title = 64)
var BYTES_PER_EVENT = 65; 
// How many minutes the phone will wait before bothering Google for a fresh update
var CACHE_MINUTES = 30;
var ACK_TIMEOUT_MS = 10000;
var MAX_CHUNK_RETRIES = 4;
// The URL of your deployed Google Apps Script (The Proxy)
var PROXY_URL = 'https://script.google.com/macros/s/AKfycbx1jdy_MFdaIuR6OwV9SvaXv7_EZWHBQboY8XjLEO2xFOkAEfefh87OWKBRIzXEeuM5tg/exec';
var latestEvents = [];
function sourceSignature() { return JSON.stringify(getCalendarSources()); }
function restoreDetailCache() {
  if (localStorage.getItem('detail_cache_sources') !== sourceSignature()) return false;
  try {
    var restored = JSON.parse(localStorage.getItem('detail_cache_events') || 'null');
    if (!Array.isArray(restored)) return false;
    latestEvents = restored.slice(0, MAX_EVENTS);
    return true;
  } catch (err) { return false; }
}
function persistDetailCache(events) {
  try {
    localStorage.setItem('detail_cache_events', JSON.stringify(events));
    localStorage.setItem('detail_cache_sources', sourceSignature());
  } catch (err) { console.log('JS: Detail metadata cache unavailable'); }
}
function cacheIsFresh() {
  var stamp = Number(localStorage.getItem('last_fetch_time'));
  return stamp > 0 && Date.now() - stamp < CACHE_MINUTES * 60000;
}


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
    if (req.status === 0) {
      return finish('No HTTP response (network, redirect, authentication, or CORS)');
    }
    if (req.status !== 200) return finish('HTTP ' + req.status);
    try {
      var data = JSON.parse(req.responseText);
      if (!Array.isArray(data)) {
        var explanation = data && typeof data === 'object' ?
          (data.error || data.message || data.status || 'Unexpected JSON object') :
          'Unexpected JSON value';
        throw new Error('ICS proxy returned an error instead of events: ' + String(explanation).slice(0, 160));
      }
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
  req.onerror = function() { finish('Network error / response blocked'); };
  req.ontimeout = function() { finish('Request timeout'); };
  try { req.send(null); } catch (e) { finish('Request failed'); }
}

// Fetch up to three feeds sequentially to limit load on phones and the proxy.
// Commit a new combined calendar only when ALL configured feeds succeed.
// This is a separate experimental hybrid app: plugin on by default,
// existing ICS feeds optional, and explicit title-only deduplication.
var pluginSubscription=null;
var pluginEvents=[];
var pluginState='pending';
var pluginRevision=0;
var lastCombinedSignature='';
var combineTimer=null;
var pluginStartupTimer=null;
var syncAfterCurrent=false;
var activeIcsEvents=[];
var icsState='pending';
function pluginEnabled() { return localStorage.getItem('hybrid_plugin_enabled') !== 'false'; }
function pluginColor() {
  var n=Number(localStorage.getItem('hybrid_plugin_color') || '4');
  return n>=1 && n<=6 ? n : 4;
}
function sameTitle(event) {
  return String(event.title || '').trim().replace(/\s+/g,' ').toLocaleLowerCase();
}
function normalizeEventWindow(events) {
  var now=Date.now(), limit=now+7*86400000;
  return events.filter(function(ev) {
    var start=Number(ev.timestampMs), end=Number(ev.endTimestampMs||start);
    return !!ev.title && isFinite(start) && start>0 && start<limit &&
      isFinite(end) && end>=now && end>=start;
  });
}
function dedupeByTitle(events) {
  // Exactly as requested: two entries with the same normalized title,
  // even if on different dates, become ONE event. Earliest wins.
  var seen={},result=[];
  events.sort(function(a,b){return Number(a.timestampMs)-Number(b.timestampMs) ||
    (a.fromPlugin===b.fromPlugin?0:a.fromPlugin?-1:1);});
  events.forEach(function(ev) {
    var key=sameTitle(ev);
    if (!key || Object.prototype.hasOwnProperty.call(seen,key)) return;
    seen[key]=true;
    result.push(ev);
  });
  return result;
}
function propertyValue(properties, names) {
  for(var i=0;i<names.length;i++) {
    var p=properties[names[i]];
    if (p===undefined || p===null) continue;
    if (typeof p==='string' || typeof p==='number') return p;
    if (p.timestamp && p.timestamp.value!==undefined) return p.timestamp.value;
    if (p.longText && p.longText.text!==undefined) return p.longText.text;
    if (p.shortText && p.shortText.text!==undefined) return p.shortText.text;
    if (p.numericValue && p.numericValue.value!==undefined) return p.numericValue.value;
    if (p.boolean && p.boolean.value!==undefined) return p.boolean.value;
  }
  return undefined;
}
function timestampMs(raw) {
  if (raw===undefined || raw===null || raw==='') return NaN;
  var n=typeof raw==='number'?raw:Number(raw);
  if (isFinite(n)) return n<100000000000?n*1000:n;
  var t=Date.parse(String(raw));
  return isFinite(t)?t:NaN;
}
function normalizePluginEnvelope(envelope) {
  if (!envelope || !Array.isArray(envelope.instances)) return [];
  var entries=[];
  envelope.instances.forEach(function(instance) {
    var p=instance && instance.properties || {};
    var title=propertyValue(p,['title','summary','name']);
    var start=propertyValue(p,['start','start_time','startTime','start_date','startDate','time','timestamp']);
    var finish=propertyValue(p,['end','end_time','endTime','end_date','endDate']);
    var timestamp=timestampMs(start), end=timestampMs(finish);
    if (!isFinite(timestamp) || !title) return;
    if (!isFinite(end) || end<timestamp) end=timestamp;
    entries.push({
      title:String(title),timestampMs:timestamp,endTimestampMs:end,
      location:String(propertyValue(p,['location','venue'])||''),
      description:String(propertyValue(p,['description','notes'])||''),
      url:String(propertyValue(p,['url','link'])||''),
      organizer:String(propertyValue(p,['organizer'])||''),
      isAllDay:!!propertyValue(p,['all_day','allDay','isAllDay']),
      calendarColor:pluginColor(),fromPlugin:true,
      uid:String(instance.instanceId||'')
    });
  });
  if (envelope.instances.length && !entries.length) {
    var example=envelope.instances[0].properties||{};
    console.log('Hybrid: Plugin calendar fields not recognized: '+Object.keys(example).join(', '));
  }
  return normalizeEventWindow(entries);
}
function scheduleCombinedSync() {
  if (combineTimer) clearTimeout(combineTimer);
  combineTimer=setTimeout(function(){combineTimer=null;pushCombinedCalendar(false);},750);
}
function pushCombinedCalendar(force) {
  if (syncBusy || activeTransfer) { syncAfterCurrent=true; return; }
  // Don't overwrite good cached data with an empty result from an unavailable plugin.
  if (pluginEnabled() && pluginState==='pending') return;
  if (getCalendarSources().length && icsState==='pending') return;
  if (pluginState==='failed' && icsState!=='ready') {
    console.log('Hybrid: No usable calendar source, keeping last successful watch data');
    return;
  }
  var selected=dedupeByTitle(normalizeEventWindow(
    (pluginEnabled()?pluginEvents:[]).concat(activeIcsEvents)));
  var watchInfo=Pebble.getActiveWatchInfo?Pebble.getActiveWatchInfo():null;
  var count=watchInfo && watchInfo.platform==='aplite'?20:MAX_EVENTS;
  selected=selected.slice(0,count);
  var sig=JSON.stringify(selected);
  if (!force && sig===lastCombinedSignature) return;
  syncBusy=true;
  sendEventByteArray(selected,0,function(ok) {
    syncBusy=false;
    if (syncAfterCurrent) { syncAfterCurrent=false; scheduleCombinedSync(); }
    if(ok) {
      latestEvents=selected;
      lastCombinedSignature=sig;
      persistDetailCache(selected);
      detailCacheRestored=true;
      lastWatchTransferAt=Date.now();
      localStorage.setItem('last_fetch_time',String(Date.now()));
      console.log('Hybrid: Synced '+selected.length+' unique event titles');
    } else {
      Pebble.sendAppMessage({SYNC_STATUS:'error'},function(){},function(){});
    }
  });
}
function fetchIcal(force) {
  var urls=getCalendarSources();
  if (!urls.length) {
    activeIcsEvents=[];
    icsState='ready';
    pushCombinedCalendar(!!force);
    return;
  }
  // ICS-only refresh; plugin updates are handled independently.
  if (icsState==='loading') return;
  icsState='loading';
  var all=[],index=0;
  function next() {
    if (index>=urls.length) {
      activeIcsEvents=all;
      icsState='ready';
      pushCombinedCalendar(!!force);
      return;
    }
    var source=urls[index++];
    fetchOneCalendar(source.url,function(err,events) {
      if (err) {
        console.log('Hybrid: ICS '+index+' failed: '+err);
        icsState='failed';
        if (pluginEnabled() && pluginState==='ready') pushCombinedCalendar(!!force);
        else Pebble.sendAppMessage({SYNC_STATUS:'error'},function(){},function(){});
        return;
      }
      events.forEach(function(ev){ev.calendarColor=source.color;all.push(ev);});
      next();
    });
  }
  next();
}
function startPluginCalendar() {
  if (!pluginEnabled()) {
    pluginState='disabled';pluginEvents=[];
    return;
  }
  if (typeof Pebble.subscribeToSource!=='function') {
    pluginState='failed';
    console.log('Hybrid: Plugin API unavailable. Enable Plugins Preview in Pebble mobile Settings.');
    return;
  }
  if(pluginSubscription) return;
  pluginState='pending';
  try {
    pluginStartupTimer=setTimeout(function(){
      pluginStartupTimer=null;
      if(pluginState==='pending') {
        console.log('Hybrid: Native calendar not responding; using available ICS data');
        pluginState='failed';
        scheduleCombinedSync();
      }
    },12000);
    pluginSubscription=Pebble.subscribeToSource({
      category:'calendar',item:'event',
      onData:function(envelope) {
        pluginEvents=normalizePluginEnvelope(envelope);
        pluginState='ready';
        pluginRevision++;
        if (pluginStartupTimer) { clearTimeout(pluginStartupTimer);pluginStartupTimer=null; }
        scheduleCombinedSync();
      },
      onError:function(error) {
        console.log('Hybrid: calendar/event error: '+(error && error.code)+' '+(error && error.message));
        if(pluginState==='pending') pluginState='failed';
        if(pluginStartupTimer) { clearTimeout(pluginStartupTimer);pluginStartupTimer=null; }
        scheduleCombinedSync();
      }
    });
  } catch(err) {
    pluginState='failed';
    console.log('Hybrid: Could not subscribe to native calendar: '+err);
  }
}
function stopPluginCalendar() {
  if(pluginStartupTimer) { clearTimeout(pluginStartupTimer);pluginStartupTimer=null; }
  if(pluginSubscription && pluginSubscription.unsubscribe) pluginSubscription.unsubscribe();
  pluginSubscription=null;pluginState='disabled';pluginEvents=[];
}

/**
 * Converts JS objects into raw bytes and sends them to the watch in manageable chunks.
 * This function calls itself recursively until all events are sent.
 * @param {Array} events - The full array of parsed calendar events
 * @param {number} start - The index of the array to start this specific chunk at
 */
// Compact binary transfer with the same end-to-end ACK principles used by
// VitalGuage: unique transaction ID, checksum, retry, and duplicate-packet handling.
function chunkBytes(events, offset, count) {
  var buf = new ArrayBuffer(count * BYTES_PER_EVENT);
  var view = new DataView(buf);
  var bytes = new Uint8Array(buf);
  for (var i = 0; i < count; i++) {
    var base = i * BYTES_PER_EVENT;
    var ev = events[offset+i];
    view.setUint32(base, Math.floor(Number(ev.timestampMs)/1000), true);
    view.setUint32(base+4, Math.floor(Number(ev.endTimestampMs || ev.timestampMs)/1000), true);
    encodeTitleUtf8(ev.title || 'No Title', bytes, base+8);
    bytes[base+64] = ev.calendarColor || 1;
  }
  return Array.prototype.slice.call(bytes);
}
function calculateChecksum(packets) {
  var a=1,b=0;
  for (var i=0;i<packets.length;i++) {
    for (var j=0;j<packets[i].length;j++) {
      a=(a+packets[i][j])%65521;
      b=(b+a)%65521;
    }
  }
  return ((b*65536+a)>>>0);
}
function finishTransfer(transfer, ok) {
  if (activeTransfer !== transfer) return;
  if (transfer.timer) clearTimeout(transfer.timer);
  activeTransfer=null;
  transfer.done(ok);
}
function retryChunk(transfer, reason) {
  if (activeTransfer !== transfer) return;
  if (transfer.timer) clearTimeout(transfer.timer);
  transfer.timer=null;
  if (transfer.retries >= MAX_CHUNK_RETRIES) {
    console.log('JS: Transfer '+transfer.id+' failed chunk '+transfer.index+': '+reason);
    finishTransfer(transfer, false);
    return;
  }
  transfer.retries++;
  var delay=Math.min(2000, 350*Math.pow(2,transfer.retries-1));
  console.log('JS: Retrying chunk '+transfer.index+' ('+transfer.retries+'/'+MAX_CHUNK_RETRIES+')');
  transfer.timer=setTimeout(function() {
    if (activeTransfer===transfer) sendChunk(transfer);
  },delay);
}
function sendChunk(transfer) {
  if (activeTransfer !== transfer) return;
  if (transfer.timer) clearTimeout(transfer.timer);
  var index=transfer.index;
  var msg={
    TOTAL_EVENTS:transfer.events.length,
    CHUNK_INDEX:index,
    SYNC_ID:transfer.id,
    SYNC_CHECKSUM:transfer.checksum
  };
  // Pebble cannot reliably represent an empty byte-array tuple on all targets.
  if (transfer.events.length) msg.EVENT_DATA=transfer.packets[index];
  // Start the ACK timer BEFORE sending to avoid missing a fast watch reply.
  transfer.timer=setTimeout(function() {
    if (activeTransfer===transfer && transfer.index===index) retryChunk(transfer,'ACK timeout');
  },ACK_TIMEOUT_MS);
  Pebble.sendAppMessage(msg, function() {
    // Bluetooth transport ACK alone is not proof that watch committed the data.
  }, function(err) {
    if (activeTransfer===transfer && transfer.index===index)
      retryChunk(transfer,'AppMessage send failed');
  });
}
function sendEventByteArray(events, start, done) {
  if (activeTransfer) { done(false); return; }
  var packets=[];
  if (!events.length) packets.push([]);
  for (var offset=0;offset<events.length;offset+=CHUNK_SIZE)
    packets.push(chunkBytes(events,offset,Math.min(CHUNK_SIZE,events.length-offset)));
  var id=((Date.now()%2147480000) + (++transferSequence)) % 2147483647;
  if (!id) id=1;
  var transfer={
    id:id, events:events, packets:packets, checksum:calculateChecksum(packets),
    index:0, retries:0, timer:null, done:done
  };
  activeTransfer=transfer;
  sendChunk(transfer);
}
function handleChunkAck(payload) {
  if (!activeTransfer || payload.SYNC_ID === undefined || payload.CHUNK_ACK === undefined) return;
  var transfer=activeTransfer;
  if (Number(payload.SYNC_ID)!==transfer.id || Number(payload.CHUNK_ACK)!==transfer.index) {
    console.log('JS: Ignoring stale or duplicate chunk ACK');
    return;
  }
  if (transfer.timer) clearTimeout(transfer.timer);
  transfer.timer=null;
  transfer.index++;
  transfer.retries=0;
  if (transfer.index >= transfer.packets.length) {
    console.log('JS: Watch verified all '+transfer.packets.length+' chunks, transaction '+transfer.id);
    finishTransfer(transfer,true);
  } else {
    sendChunk(transfer);
  }
}

// Keep detailed metadata on the phone rather than in the watch's event array.
// AppMessage strings are UTF-8; bound the total text for older watch buffers.
function detailText(event) {
  if (!event) return 'Details unavailable. Refresh calendar.';
  var out = [];
  function add(label, value, maximum) {
    if (value === null || value === undefined || value === '') return;
    var clean = String(value).replace(/\r/g, '').trim();
    if (clean.length > maximum) clean = clean.slice(0, maximum - 1) + '…';
    if (clean) out.push(label + '\n' + clean);
  }
  if (event.isAllDay) out.push('All day');
  add('Location', event.location, 120);
  add('Description', event.description, 180);
  add('Organizer', event.organizer, 60);
  add('Link', event.url, 100);
  add('Status', event.status, 20);
  add('Category', Array.isArray(event.categories) ? event.categories.join(', ') : event.categories, 40);
  var value = out.join('\n\n') || 'No additional details';
  // Limit by UTF-8 bytes to stay within Pebble's AppMessage inbox.
  var result = '', length = 0;
  for (var i = 0; i < value.length; i++) {
    var cp = value.charCodeAt(i);
    var width = cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < value.length) {
      var next = value.charCodeAt(i + 1);
      if (next >= 0xDC00 && next <= 0xDFFF) {
        if (length + 4 > 345) break;
        result += value.charAt(i++) + value.charAt(i);
        length += 4;
        continue;
      }
    }
    if (length + width > 345) break;
    result += value.charAt(i);
    length += width;
  }
  return result;
}

// --- App Event Listeners ---

// Fired the moment the app boots up on the watch
Pebble.addEventListener('ready', function() {
  console.log('Hybrid: Companion started');
  detailCacheRestored=restoreDetailCache();
  startPluginCalendar();
  // ICS is optional: if no URL is supplied, only the plugin contributes events.
  fetchIcal(false);
});

// Fired when the watch specifically asks the phone for help (The "Help Signal")
// This happens if the watch's internal storage gets wiped and it needs data IMMEDIATELY
Pebble.addEventListener('appmessage', function(e) {
  var payload = (e && e.payload) || {};
  if (payload.CHUNK_ACK !== undefined) {
    handleChunkAck(payload);
    return;
  }
  if (payload.DETAIL_REQUEST !== undefined) {
    var index = Number(payload.DETAIL_REQUEST);
    var message = detailText(latestEvents[index]);
    Pebble.sendAppMessage({DETAIL_TEXT: message, DETAIL_TOKEN: Number(payload.DETAIL_TOKEN) || 0}, function(){},
      function(){ console.log('JS: Details not delivered to watch'); });
    return;
  }
  // Watch-side settings also update Clay's persisted phone configuration.
  var prefNames = ['THEME_MODE','FONT_SIZE','SHOW_PAST','TIME_FORMAT','DETAIL_STATUS_BAR'];
  if (payload.WATCH_PREFS !== undefined) {
    var settings = {};
    for (var i=0;i<prefNames.length;i++) {
      var key=prefNames[i];
      if (payload[key] !== undefined) {
        settings[key]=String(payload[key]);
        localStorage.setItem('watch_pref_'+key,settings[key]);
      }
    }
    clay.setSettings(settings);
    return;
  }
  if (payload.SYNC_COMMAND === 'refresh') {
    console.log('Hybrid: Manual refresh');
    if (pluginEnabled()) {
      stopPluginCalendar();
      startPluginCalendar();
    }
    icsState='pending';
    fetchIcal(true);
  } else if (payload.SYNC_COMMAND === 'sync') {
    if (activeTransfer || syncBusy) return;
    if (cacheIsFresh() && detailCacheRestored && Date.now()-lastWatchTransferAt>10000) {
      sendEventByteArray(latestEvents,0,function(ok) {
        if(ok) lastWatchTransferAt=Date.now();
      });
    } else if (!detailCacheRestored) {
      fetchIcal(false);
      startPluginCalendar();
    }
  }
});

// Fired when the user taps the "Settings" gear icon in the Pebble app on their phone
Pebble.addEventListener('showConfiguration', function() { 
  // Generate and open the Clay settings HTML page
  // The watch may have changed preferences independently of the phone.
  var prefs = {};
  ['THEME_MODE','FONT_SIZE','SHOW_PAST','TIME_FORMAT','DETAIL_STATUS_BAR'].forEach(function(key){
    var value=localStorage.getItem('watch_pref_'+key);
    if (value !== null) prefs[key]=value;
  });
  prefs.PLUGIN_ENABLED=pluginEnabled()?'true':'false';
  prefs.PLUGIN_COLOR=String(pluginColor());
  clay.setSettings(prefs);
  Pebble.openURL(clay.generateUrl()); 
});

// Store all three URLs locally; only send the time preference to the watch.
Pebble.addEventListener('webviewclosed', function(e) {
  if (!e || !e.response || e.response === 'CANCELLED') return;
  var dict;
  try { dict = clay.getSettings(e.response); }
  catch (err) { console.log('JS: Invalid settings response'); return; }

  var beforeSources=sourceSignature();
  var wasPluginEnabled=pluginEnabled();
  var oldPluginColor=pluginColor();
  if (dict[messageKeys.PLUGIN_ENABLED]!==undefined)
    localStorage.setItem('hybrid_plugin_enabled',String(dict[messageKeys.PLUGIN_ENABLED])==='true'?'true':'false');
  if (dict[messageKeys.PLUGIN_COLOR]!==undefined)
    localStorage.setItem('hybrid_plugin_color',String(dict[messageKeys.PLUGIN_COLOR]));
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
  var watchPrefs = {};
  ['TIME_FORMAT','FONT_SIZE','THEME_MODE','SHOW_PAST','DETAIL_STATUS_BAR'].forEach(function(key) {
    if (dict[messageKeys[key]] !== undefined) {
      watchPrefs[key] = String(dict[messageKeys[key]]);
      localStorage.setItem('watch_pref_'+key, watchPrefs[key]);
    }
  });
  if (Object.keys(watchPrefs).length) {
    Pebble.sendAppMessage(watchPrefs, function(){},
      function(){ console.log('JS: Watch preference update failed'); });
  }
  if (wasPluginEnabled!==pluginEnabled() || oldPluginColor!==pluginColor()) {
    stopPluginCalendar();
    if(pluginEnabled()) startPluginCalendar();
    else { pluginState='disabled'; scheduleCombinedSync(); }
  }
  if (beforeSources !== sourceSignature()) {
    localStorage.removeItem('last_fetch_time');
    localStorage.removeItem('detail_cache_events');
    detailCacheRestored=false;
    icsState='pending';
    fetchIcal(true);
  } else if (wasPluginEnabled!==pluginEnabled() || oldPluginColor!==pluginColor()) {
    scheduleCombinedSync();
  }
});
