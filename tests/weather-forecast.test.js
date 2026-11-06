'use strict';
process.env.TZ='America/New_York';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const src=fs.readFileSync('src/pkjs/index.js','utf8');
const start=src.indexOf('var forecastPending=null;');
const end=src.indexOf('// Optional native Weather plugin: sunrise/sunset',start);
assert(start>=0 && end>start,'daily forecast adapter present');
const sent=[],ctx={
  console:{log:()=>{}},Date,Number,Math,JSON,Array,Uint8Array,DataView,
  localStorage:{getItem:()=>null,setItem:()=>{}},
  Pebble:{sendAppMessage:(msg,ok)=>{sent.push(msg);if(ok)ok();}},
  activeTransfer:null,syncBusy:false,
  setTimeout:()=>1,clearTimeout:()=>{}
};
vm.createContext(ctx);vm.runInContext(src.slice(start,end),ctx);
const today=new Date();today.setHours(0,0,0,0);
const startEpoch=Math.floor(today.getTime()/1000);
const days=5;
const series=(type,values,unit)=>({series:{
  start:startEpoch,stepSec:86400,
  [type]:{values:values,unit:unit}
}});
const envelope={instances:[{properties:{
  daily_high:series('numericValue',[74,76,78,81,79],'°F'),
  daily_low:series('numericValue',[55,57,59,62,60],'°F'),
  daily_condition_code:series('longText',['sun','partly_cloudy','cloudy','light_rain','heavy_snow'])
}}]};
const forecast=ctx.forecastFromEnvelope(envelope);
assert.equal(forecast.length,5);
assert.deepEqual(Array.from(forecast,x=>x.icon),[1,2,3,4,6]);
assert.deepEqual(Array.from(forecast,x=>[x.high,x.low]),[[74,55],[76,57],[78,59],[81,62],[79,60]]);
const packed=ctx.packForecast(forecast);
assert.equal(packed.length,35);
assert.equal(packed[4],74);
assert.equal(packed[5],55);
assert.equal(packed[6],1);
for(const day of forecast) {
  const date=new Date(day.dateSec*1000);
  assert.equal(date.getHours(),12);
}
ctx.queueForecast(forecast);
assert.equal(sent.length,1,'forecast delivered independently');
assert.equal(sent[0].WEATHER_FORECAST.length,35);
ctx.queueForecast(forecast);
assert.equal(sent.length,1,'unchanged weather is not retransmitted');
const missing={instances:[{properties:{
  daily_high:series('numericValue',[75,76],'°F'),
  daily_low:series('numericValue',[50,51],'°F')
}}]};
assert.equal(ctx.forecastFromEnvelope(missing).length,0,'incomplete series does not overwrite forecast');
const shifted={instances:[{properties:{
  daily_high:series('numericValue',[null,4],'°C'),
  daily_low:series('numericValue',[null,-2],'°C'),
  daily_condition_code:series('longText',['sun','rain_and_snow'])
}}]};
const result=ctx.forecastFromEnvelope(shifted);
assert.equal(result.length,1);
assert.equal(result[0].icon,7);
assert.equal(result[0].low,-2);
const raw=ctx.packForecast(result);
assert.equal(new DataView(Uint8Array.from(raw).buffer).getInt8(5),-2);
console.log('Native Weather daily forecasts, labels, icons and serialization tests passed');
