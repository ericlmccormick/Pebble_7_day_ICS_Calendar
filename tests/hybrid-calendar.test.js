'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('src/pkjs/index.js','utf8');
const now=Date.now();
function scenario(withIcs) {
  const handlers={},sent=[],timers=new Map(),storage={};
  const subs=[];
  let seq=0,fetches=0;
  if(withIcs) storage.user_ical_url_1='https://example.test/calendar.ics';
  const localStorage={
    getItem:k=>Object.prototype.hasOwnProperty.call(storage,k)?storage[k]:null,
    setItem:(k,v)=>{storage[k]=String(v);},
    removeItem:k=>{delete storage[k];}
  };
  function Request(){this.readyState=0;this.status=0;}
  Request.prototype.open=function(method,url){this.url=url;};
  Request.prototype.send=function(){
    fetches++;
    this.readyState=4;this.status=200;
    this.responseText=JSON.stringify([
      {title:'Shared',timestampMs:now+1800000,endTimestampMs:now+5400000},
      {title:'ICS Only',timestampMs:now+10800000,endTimestampMs:now+14400000}
    ]);
    this.onreadystatechange();
  };
  function Clay(){this.setSettings=()=>{};this.generateUrl=()=>'';this.getSettings=()=>({});}
  const Pebble={
    addEventListener:(name,fn)=>{handlers[name]=fn;},
    subscribeToSource:(args)=>{subs.push(args);return {unsubscribe:()=>{}};},
    sendAppMessage:(payload,ok,err)=>{
      sent.push(payload);
      if(payload.CHUNK_INDEX!==undefined)
        handlers.appmessage({payload:{SYNC_ID:payload.SYNC_ID,CHUNK_ACK:payload.CHUNK_INDEX}});
      if(ok)ok();
    },
    getActiveWatchInfo:()=>({platform:'emery'}),
    openURL:()=>{}
  };
  const ctx={
    require:name=>name==='pebble-clay'?Clay:name==='message_keys'?{}:[],
    Pebble,localStorage,XMLHttpRequest:Request,
    setTimeout:(fn,delay)=>{const id=++seq;timers.set(id,{fn,delay});return id;},
    clearTimeout:id=>timers.delete(id),
    console
  };
  vm.createContext(ctx);
  vm.runInContext(source,ctx);
  handlers.ready();
  assert.equal(subs.length,1,'Native calendar plugin subscribed by default');
  assert.equal(fetches,withIcs?1:0,'ICS only queried when configured');
  const instances=[
    {instanceId:'one',properties:{
      title:{longText:{text:'Shared'}},
      starts_at:{timestamp:{value:Math.floor((now+1800000)/1000)}},
      ends_at:{timestamp:{value:Math.floor((now+5400000)/1000)}},
      location:{longText:{text:'Test room'}},
      calendar:{shortText:{text:'Personal'}}
    }},
    {instanceId:'two',properties:{
      title:{longText:{text:'Plugin Only'}},
      start:{timestamp:{value:Math.floor((now+7200000)/1000)}},
      ends_at:{timestamp:{value:Math.floor((now+9000000)/1000)}}
    }},
    {instanceId:'three',properties:{
      title:{shortText:{text:' Plugin Only '}}},
      start:{timestamp:{value:Math.floor((now+86400000)/1000)}}
    }}
  ];
  subs[0].onData({instances});
  const queued=Array.from(timers.entries()).find(([,t])=>t.delay===750);
  assert(queued,'plugin update debounced');
  timers.delete(queued[0]);queued[1].fn();
  const events=JSON.parse(storage.detail_cache_events);
  assert.equal(events.length,withIcs?3:2);
  assert.equal(events.filter(x=>x.title.trim()==='Shared').length,1);
  assert.equal(events.filter(x=>x.title.trim()==='Plugin Only').length,1);
  assert(events.some(x=>x.location==='Test room'),'plugin location retained');
  assert(events.some(x=>x.calendarName==='Personal'),'native calendar name retained');
  assert(JSON.parse(storage.hybrid_calendar_names).includes('Work'),'calendar names discovered for color settings');
  assert(sent.some(x=>x.EVENT_DATA),'watch received compact event transfer');
  assert(storage.last_fetch_time,'only successful acknowledged transfer saved');
}
scenario(false);
scenario(true);
console.log('Hybrid plugin-only and mixed-calendar integration tests passed');