'use strict';
process.env.TZ='America/New_York';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('src/pkjs/index.js','utf8');
const begin=source.indexOf('function localMidnightForDateOnly(');
const end=source.indexOf('function fetchOneCalendar(',begin);
assert(begin>=0 && end>begin);
const context={Date,Number,isFinite};vm.createContext(context);
vm.runInContext(source.slice(begin,end),context);
function check(y,m,d) {
  const utc=Date.UTC(y,m-1,d),next=Date.UTC(y,m-1,d+1);
  const local=new Date(y,m-1,d).getTime();
  const endLocal=new Date(y,m-1,d+1).getTime();
  const event={title:'Holiday',isAllDay:true,timestampMs:utc,endTimestampMs:next};
  context.normalizeAllDayEvent(event);
  assert.equal(event.timestampMs,local);
  assert.equal(event.endTimestampMs,endLocal);
  assert.equal(new Date(event.timestampMs).getHours(),0);
}
check(2026,11,6); // EST prior-day 7 PM regression
check(2026,7,4);  // EDT prior-day 8 PM regression
check(2026,3,8);  // DST spring forward
check(2026,11,1);// DST fall back
const local=new Date(2026,10,6).getTime();
const native={isAllDay:true,timestampMs:local,endTimestampMs:new Date(2026,10,7).getTime()};
context.normalizeAllDayEvent(native);
assert.equal(native.timestampMs,local);
const timed={isAllDay:false,timestampMs:Date.UTC(2026,10,6)};
context.normalizeAllDayEvent(timed);
assert.equal(timed.timestampMs,Date.UTC(2026,10,6));
const absent={isAllDay:true,timestampMs:Date.UTC(2026,10,6)};
context.normalizeAllDayEvent(absent);
assert.equal(absent.endTimestampMs,new Date(2026,10,7).getTime());
assert(source.includes('ev.isAllDay ? 0x80 : 0'));
console.log('ICS all-day timezone/DST/flag regressions passed');
