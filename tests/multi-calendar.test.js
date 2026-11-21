// Run with: node tests/multi-calendar.test.js
'use strict';
const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const source = fs.readFileSync('src/pkjs/index.js', 'utf8');
const begin = source.indexOf('function getCalendarSources()');
const end = source.indexOf('/**\n * Converts JS objects', begin);
assert(begin !== -1 && end > begin);
const values = {};
const requests = [];
const transmitted = [];
const statuses = [];
let failFeed;
let revision = 0;
const now = Date.now();
const settings = {
  getItem: function(k) { return Object.prototype.hasOwnProperty.call(values,k) ? values[k] : null; },
  setItem: function(k,v) { values[k] = v; }
};
function Request() {
  this.readyState = 0;
  this.status = 0;
  this.responseText = '';
}
Request.prototype.open = function(method,url) { this.url = url; };
Request.prototype.send = function() {
  requests.push(this.url);
  if (failFeed === this.url) {
    this.ontimeout();
    return;
  }
  const i = this.url.includes('%2Fone') ? 1 : 2;
  this.status = 200;
  this.readyState = 4;
  this.responseText = JSON.stringify(i === 1 ?
    [{timestampMs:now+3000,title:'Trois'},{timestampMs:now+1000,title:revision?'Un updated':'Un'}] :
    [{timestampMs:now+2000,title:'Deux'}]);
  this.onreadystatechange();
};
const context = {
  localStorage: settings, XMLHttpRequest: Request, PROXY_URL: 'https://example.test/proxy',
  syncBusy:false, pendingRefresh:false, MAX_EVENTS:48,
  Pebble:{getActiveWatchInfo:function(){return {platform:'emery'};},sendAppMessage:function(payload){statuses.push(payload.SYNC_STATUS);}},
  sendEventByteArray:function(events,start,done){transmitted.push(events);done(true);},
  console:console
};
vm.createContext(context);
vm.runInContext(source.slice(begin,end), context);
values.user_ical_url='https://example.test/old';
assert.equal(context.getCalendarSources().length,1,'legacy URL preserved');
values.user_ical_url_1='https://example.test/one';
values.user_ical_url_2='https://example.test/two';
values.user_ical_url_3='';
assert.equal(context.getCalendarSources().length,2);
values.user_ical_color_1 = '4';
values.user_ical_color_2 = '6';
assert.equal(context.getCalendarSources()[0].color,4);
context.fetchIcal();
assert.equal(requests.length,2);
assert.equal(transmitted.length,1);
assert.deepEqual(Array.from(transmitted[0],e=>e.title),['Un','Deux','Trois']);
assert.equal(context.syncBusy,false);
assert.deepEqual(Array.from(transmitted[0],e=>e.calendarColor),[4,6,4]);
failFeed = requests[1];
revision = 1;
context.fetchIcal();
assert.deepEqual(Array.from(transmitted[1],e=>e.title),['Un updated','Deux','Trois']);
assert.equal(statuses.at(-1), 'Stale: 2');
values.user_ical_url_2 = 'https://example.test/replacement';
failFeed = requests[1].replace('%2Ftwo', '%2Freplacement');
context.fetchIcal();
assert.deepEqual(Array.from(transmitted[2],e=>e.title),['Un updated','Trois']);
assert.equal(statuses.at(-1), 'Stale: 2');
values.user_ical_url_1 = '';
context.fetchIcal();
assert.equal(transmitted.length, 3, 'total failure preserves the watch snapshot');
assert.equal(statuses.at(-1), 'error');
console.log('Multi-calendar merge and timeout recovery tests passed');
