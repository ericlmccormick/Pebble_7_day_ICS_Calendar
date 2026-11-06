'use strict';
// All-day regressions: UTC-midnight date marker -> local calendar date, not
// previous evening; DST boundaries; native already-local midnight; wire flag.
process.env.TZ='America/New_York';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const src=fs.readFileSync('src/pkjs/index.js','utf8');
const begin=src.indexOf('function localMidnightForDateOnly(');
const end=src.indexOf('function dedupeByTitle(',begin);
assert(begin>0 && end>begin);
const context={Date,console};
vm.createContext(context);
vm.runInContext(src.slice(begin,end),context);
function isoUTC(y,m,d) {return Date.UTC(y,m-1,d);}
function local(y,m,d) {return new Date(y,m-1,d).getTime();}
function testDate(y,m,d) {
  const start=isoUTC(y,m,d),end=isoUTC(y,m,d+1);
  const corrected=context.normalizeAllDayEvent({
    title:'Day off',isAllDay:true,timestampMs:start,endTimestampMs:end
  });
  assert.equal(corrected.timestampMs,local(y,m,d));
  assert.equal(corrected.endTimestampMs,local(y,m,d+1));
  assert.equal(new Date(corrected.timestampMs).getHours(),0);
  assert.equal(new Date(corrected.timestampMs).getDate(),d);
}
testDate(2026,11,6); // 00:00Z => previous day at 7pm before fix
testDate(2026,7,4);  // 00:00Z => previous day at 8pm in EDT
testDate(2026,3,8);  // spring forward: one calendar day is 23 hours
testDate(2026,11,1);// fall back: one calendar day is 25 hours
const localStart=local(2026,11,6);
assert.equal(context.normalizeAllDayEvent({
  title:'Native',isAllDay:true,timestampMs:localStart,
  endTimestampMs:local(2026,11,7)
}).timestampMs,localStart,'already local midnight not shifted');
const missing=context.normalizeAllDayEvent({
  title:'One-day event',isAllDay:true,timestampMs:isoUTC(2026,11,6)
});
assert.equal(missing.endTimestampMs,local(2026,11,7),'missing end one full local calendar day');
const timed=context.normalizeAllDayEvent({
  title:'Meeting',isAllDay:false,timestampMs:isoUTC(2026,11,6)
});
assert.equal(timed.timestampMs,isoUTC(2026,11,6),'timed appointments never shifted');

// All-day marker occupies the upper bit of the original color byte.
// No new transfer bytes or persisted event format required.
const encodeStart=src.indexOf('function encodeTitleUtf8(');
const encodeEnd=src.indexOf('// --- Core Functions ---',encodeStart);
const packStart=src.indexOf('function chunkBytes(');
const packEnd=src.indexOf('function calculateChecksum(',packStart);
assert(encodeStart>0 && encodeEnd>encodeStart && packStart>0 && packEnd>packStart);
const packContext={ArrayBuffer,DataView,Uint8Array,Array,Math,Number,BYTES_PER_EVENT:65};
vm.createContext(packContext);
vm.runInContext(src.slice(encodeStart,encodeEnd)+src.slice(packStart,packEnd),packContext);
const bytes=packContext.chunkBytes([
  {title:'All day',isAllDay:true,calendarColor:4,timestampMs:localStart,endTimestampMs:local(2026,11,7)},
  {title:'Meeting',isAllDay:false,calendarColor:3,timestampMs:localStart,endTimestampMs:localStart+3600000}
],0,2);
assert.equal(bytes.length,130,'wire format unchanged');
assert.equal(bytes[64],0x84,'all-day flag and calendar color are preserved');
assert.equal(bytes[129],3,'timed event has no all-day flag');
console.log('All-day UTC/local date, DST, and binary payload tests passed');
