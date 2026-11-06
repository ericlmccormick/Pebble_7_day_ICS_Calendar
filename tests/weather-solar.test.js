'use strict';
// Verify native Weather envelope parsing, calendar coexistence, and solar fallback.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('src/pkjs/index.js','utf8');
const start=source.indexOf('var weatherSubscription=null;');
const end=source.indexOf('function stopPluginCalendar()',start);
assert(start>0 && end>start,'weather adapter present');
const signals=[];
const storage={};
const listeners=[];
let timerId=0;
const ctx={
  console:{log:()=>{}},
  localStorage:{getItem:k=>storage[k]||null,setItem:(k,v)=>storage[k]=String(v)},
  Pebble:{
    subscribeToSource:options=>{
      listeners.push(options);
      return {unsubscribe:()=>{}};
    },
    sendAppMessage:(payload,success)=>{signals.push(payload);if(success)success();}
  },
  activeTransfer:null,syncBusy:false,
  setTimeout:(callback,ms)=>++timerId,
  clearTimeout:()=>{}
};
vm.createContext(ctx);
vm.runInContext(source.slice(start,end),ctx);
const now=Math.floor(Date.now()/1000);
const today=new Date();
const rise=new Date(today.getFullYear(),today.getMonth(),today.getDate(),6,15).getTime()/1000;
const set=new Date(today.getFullYear(),today.getMonth(),today.getDate(),18,5).getTime()/1000;
const envelope={instances:[{instanceId:'location',properties:{
  sunrise:{timestamp:{value:rise}},
  sunset:{timestamp:{value:set}}
}}]};
const solar=ctx.solarFromEnvelope(envelope);
assert.equal(solar.sunrise,rise);
assert.equal(solar.sunset,set);
assert.equal(ctx.solarFromEnvelope({instances:[]}),null);
assert.equal(ctx.solarFromEnvelope({instances:[{properties:{
  sunrise:{timestamp:{value:rise-86400}},
  sunset:{timestamp:{value:set-86400}}
}}]}),null,'yesterday rejected');
assert.equal(ctx.solarFromEnvelope({instances:[{properties:{
  sunrise:{timestamp:{value:rise}},
  sunset:{timestamp:{value:rise-10}}
}}]}),null,'reversed solar values rejected');
ctx.startWeatherPlugin();
assert.equal(listeners.length,1);
assert.equal(listeners[0].category,'weather');
assert.equal(listeners[0].item,'location');
listeners[0].onData(envelope);
assert.equal(signals.length,1);
assert.equal(signals[0].SUNRISE_TIME,rise);
assert.equal(signals[0].SUNSET_TIME,set);
assert(storage.hybrid_solar_cache);
listeners[0].onData(envelope);
assert.equal(signals.length,1,'identical solar readings not resent');
ctx.restoreSolarTimes();
assert.equal(signals.length,1,'cache restoration avoids duplicate send');
console.log('Weather solar Auto mode regression tests passed');
