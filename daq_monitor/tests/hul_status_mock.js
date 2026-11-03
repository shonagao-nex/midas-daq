// Execute the production renderer with a minimal DOM, without MIDAS or network.
const Gio=imports.gi.Gio, ByteArray=imports.byteArray;
const [,bytes]=Gio.File.new_for_path('daq_monitor/web/daq.js').load_contents(null);
const source=ByteArray.toString(bytes);
const body=source.slice(source.indexOf('function renderHul('),source.indexOf('function updateAlarmButton'));
const nodes={};
function element(id){return nodes[id]||(nodes[id]={classList:{toggle(){}}});}
const valid=v=>v!==null&&v!==undefined&&v!=='';
const truth=v=>v===true||v===1;
function setText(id,value,fallback='—'){element(id).textContent=valid(value)?String(value):fallback;}
function status(id,label){setText(id,label);}
function renderToggle(module,enabled,locked){element('toggle').locked=locked;}
const HUL_MODULE={};
let lastRunState='STOPPED',lastControlRunState='STOPPED',lastTransitionInProgress=0,transitionPending=false;
const count=String,rate=String,dataRate=String;
eval(body);
let cases=0;
for (const run of ['STOPPED','RUNNING']) for(const present of [true,false,null]) for(const enabled of [true,false,null]) for(const comm of ['Comm OK','Disconnected','Unknown','Connected']) {
 lastRunState=lastControlRunState=run;
 const values={hulEnabled:enabled,hulConnection:comm,hulChecked:Date.now()/1000,hulEventId:10,hulEventCount:5,hulEventRate:1,hulDataRate:0.7};
 renderHul(k=>values[k],present);
 const expected=present!==true||enabled===null?'Unknown':!enabled?'Disabled':['Comm OK','Disconnected'].includes(comm)?comm:'Unknown';
 if(element('hul-scaler-1-status').textContent!==expected)throw Error('status mismatch');
 if(element('hul-scaler-1-events').textContent!=='—')throw Error('events mismatch');
 if(run==='RUNNING'&&!element('toggle').locked)throw Error('running toggle unlocked');
 cases++;
}
renderHul(k=>({hulEnabled:true,hulConnection:'Comm OK',hulChecked:1})[k],true);
if(element('hul-scaler-1-status').textContent!=='Unknown')throw Error('stale state');
print('HUL renderer: '+cases+' state combinations and stale result passed');

for (const checked of [null,undefined,0,1,NaN]) {
 renderHul(k=>({hulEnabled:true,hulConnection:'Comm OK',hulChecked:checked})[k],true);
 if(element('hul-scaler-1-status').textContent!=='Unknown')throw Error('missing/invalid/expired timestamp');
}
renderHul(k=>({hulEnabled:true,hulConnection:'Disconnected'})[k],true);
if(element('hul-scaler-1-status').textContent!=='Unknown')throw Error('legacy frontend without monitor timestamp');
renderHul(k=>({hulEnabled:false})[k],true);
if(element('hul-scaler-1-status').textContent!=='Disabled')throw Error('OFF must not depend on monitor timestamp');
print('legacy ODB, missing/expired timestamp, and Disabled regression passed');
