'use strict';
// Protocol regression tests: no cache commit before watch ACK, retry without
// losing transfer identity, stale ACK rejection, and app startup deduplication.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source=fs.readFileSync('src/pkjs/index.js','utf8');
const handlers={},sent=[],data={user_ical_url_1:'https://example.test/ics'};
let requestCount=0, timerSeq=1;
const timers=new Map();
const storage={
  getItem:k=>Object.prototype.hasOwnProperty.call(data,k)?data[k]:null,
  setItem:(k,v)=>{data[k]=String(v);},
  removeItem:k=>{delete data[k];}
};
function Request(){this.status=0;this.readyState=0;}
Request.prototype.open=function(method,url){this.url=url;};
Request.prototype.send=function(){
  requestCount++;
  this.status=200;this.readyState=4;
  const future=Date.now()+3600000;
  this.responseText=JSON.stringify([
    {title:'Réunion',timestampMs:future,endTimestampMs:future+3600000,location:'Office'},
    {title:'Gym',timestampMs:future+86400000,endTimestampMs:future+90000000},
    {title:'Meeting',timestampMs:future+172800000,endTimestampMs:future+176400000}
  ]);
  this.onreadystatechange();
};
function Clay(){this.setSettings=function(){};this.getSettings=function(){return {};};this.generateUrl=function(){return '';};}
const Pebble={
  addEventListener:(name,fn)=>handlers[name]=fn,
  getActiveWatchInfo:()=>({platform:'emery'}),
  sendAppMessage:(payload,success,failure)=>{sent.push(payload);if(success)success();},
  openURL:()=>{}
};
const context={
  require:name=>name==='pebble-clay'?Clay:name==='message_keys'?{}:[],
  Pebble,localStorage:storage,XMLHttpRequest:Request,
  setTimeout:(fn,ms)=>{const id=timerSeq++;timers.set(id,{fn,ms});return id;},
  clearTimeout:id=>timers.delete(id),console
};
vm.createContext(context);
vm.runInContext(source,context);
function ack(id,index){handlers.appmessage({payload:{SYNC_ID:id,CHUNK_ACK:index}});}
function fire(ms){
  const entry=Array.from(timers.entries()).find(([,v])=>v.ms===ms);
  assert(entry,'expected timer at '+ms);
  timers.delete(entry[0]);entry[1].fn();
}
handlers.ready();
assert.equal(requestCount,1);
assert.equal(sent.length,1,'first chunk sent');
const packet=sent[0], id=packet.SYNC_ID;
assert(packet.EVENT_DATA.length===130,'two compact events per packet');
assert.equal(data.last_fetch_time,undefined,'no cache commit before watch ACK');
ack(id+1,0);
assert.equal(sent.length,1,'stale ACK ignored');
fire(10000);fire(350);
assert.equal(sent.length,2,'first chunk was retransmitted');
assert.equal(sent[1].SYNC_ID,id,'retries retain transaction ID');
assert.deepEqual(Array.from(sent[1].EVENT_DATA),Array.from(packet.EVENT_DATA),'identical retransmission');
ack(id,0);
assert.equal(sent.length,3,'second chunk after watch acknowledgement');
ack(id,0);
assert.equal(data.last_fetch_time,undefined,'duplicate prior ACK ignored');
ack(id,1);
assert(data.last_fetch_time,'committed only after final watch ACK');
assert.equal(JSON.parse(data.detail_cache_events).length,3,'metadata persisted');
handlers.ready();
assert.equal(requestCount,1,'fresh cache prevents redundant download');
assert.equal(sent.length,3,'fresh cache does not redundantly transmit');
console.log('Transactional AppMessage regression tests passed');
