'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('src/pkjs/index.js','utf8');
const values={user_ical_url:'https://example.test/old'};
const storage={getItem:k=>Object.prototype.hasOwnProperty.call(values,k)?values[k]:null,setItem:(k,v)=>{values[k]=String(v);}};
const context={localStorage:storage,console};
vm.createContext(context);
const start=source.indexOf('function getCalendarSources()');
const end=source.indexOf('function fetchOneCalendar(',start);
assert(start>=0 && end>start);
vm.runInContext(source.slice(start,end),context);
assert.equal(context.getCalendarSources().length,1);
values.user_ical_url_1='https://example.test/a';
values.user_ical_url_2='https://example.test/b';
values.user_ical_color_1='4';
values.user_ical_color_2='6';
assert.equal(context.getCalendarSources().length,2);
assert.deepEqual(Array.from(context.getCalendarSources(),x=>x.color),[4,6]);
const first=source.indexOf('function sameTitle(');
const last=source.indexOf('function propertyValue(',first);
assert(first>=0 && last>first);
vm.runInContext(source.slice(first,last),context);
const upcoming=Date.now()+3600000;
const events=[
 {title:'Gym',timestampMs:upcoming+172800000,calendarColor:1},
 {title:' gym ',timestampMs:upcoming,calendarColor:4},
 {title:'Dentist',timestampMs:upcoming+86400000,calendarColor:2}
];
const result=context.dedupeByTitle(events);
assert.equal(result.length,2,'Duplicate titles across different dates removed');
assert.equal(result[0].calendarColor,4,'Earliest occurrence retained');
assert.equal(result[1].title,'Dentist');
console.log('ICS source selection and cross-source title dedup tests passed');
