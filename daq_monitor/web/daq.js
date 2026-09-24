"use strict";
const REFRESH_INTERVAL_MS=1000,MESSAGE_INTERVAL_MS=5000,MIDAS_SUCCESS=1,HISTORY_MAX_MS=24*60*60*1000;
const ODB=Object.freeze({
 globalSeverity:"/DAQ/Status/Global/Severity",globalSummary:"/DAQ/Status/Global/Summary",canStart:"/DAQ/Status/Global/CanStart",canStartReason:"/DAQ/Status/Global/CanStartReason",alarmSystemActive:"/Alarms/Alarm system active",acquisitionTime:"/Logger/Run duration",vmeEventLimit:"/Equipment/VME/Common/Event limit",easirocEventLimit:"/Equipment/NIM-EASIROC Physics/Common/Event limit",runNumber:"/DAQ/Status/Run/RunNumber",runState:"/DAQ/Status/Run/State",controlRunState:"/Runinfo/State",transitionInProgress:"/Runinfo/Transition in progress",runDuration:"/DAQ/Status/Run/DurationSec",startTime:"/Runinfo/Start time",stopTime:"/Runinfo/Stop time",
 vmeSeverity:"/DAQ/Status/Frontends/VME/Severity",vmeReason:"/DAQ/Status/Frontends/VME/Reason",vmeConnected:"/DAQ/Status/Frontends/VME/Connected",vmeParticipating:"/DAQ/Status/Frontends/VME/Participating",vmeEventSlip:"/DAQ/Status/Frontends/VME/EventSlipCount",easirocSeverity:"/DAQ/Status/Frontends/EASIROC/Severity",easirocReason:"/DAQ/Status/Frontends/EASIROC/Reason",easirocConnected:"/DAQ/Status/Frontends/EASIROC/Connected",easirocParticipating:"/DAQ/Status/Frontends/EASIROC/Participating",
 currentFilename:"/DAQ/Status/Logger/CurrentFilename",diskSeverity:"/DAQ/Status/Disk/Severity",diskPath:"/DAQ/Status/Disk/Path",diskFree:"/DAQ/Status/Disk/FreeGB",diskTotal:"/DAQ/Status/Disk/TotalGB",
 vmeEventRate:"/Equipment/VME/Statistics/Events per sec.",vmeDataRate:"/Equipment/VME/Statistics/kBytes per sec.",vmeEventCount:"/Equipment/VME/Statistics/Events sent",easirocEventRate:"/Equipment/NIM-EASIROC Physics/Statistics/Events per sec.",easirocDataRate:"/Equipment/NIM-EASIROC Physics/Statistics/kBytes per sec.",easirocEventCount:"/Equipment/NIM-EASIROC Physics/Statistics/Events sent",
 v792Address:"/Equipment/VME/Info/V792/BaseAddress",v792Enabled:"/Equipment/VME/Settings/V792/Enabled",v792Communication:"/Equipment/VME/Variables/V792/CommunicationOK",v1190Address:"/Equipment/VME/Info/V1190/BaseAddress",v1190Enabled:"/Equipment/VME/Settings/V1190/Enabled",v1190Communication:"/Equipment/VME/Variables/V1190/CommunicationOK",v1720eAddress:"/Equipment/VME/Info/V1720E/BaseAddress",v1720eEnabled:"/Equipment/VME/Settings/V1720E/Enabled",v1720eCommunication:"/Equipment/VME/Variables/V1720E/CommunicationOK",v775Address:"/Equipment/VME/Info/V775/BaseAddress",v775Enabled:"/Equipment/VME/Settings/V775/Enabled",v775Communication:"/Equipment/VME/Variables/V775/CommunicationOK",rpv130Address:"/Equipment/VME/Info/RPV130/BaseAddress",rpv130Enabled:"/Equipment/VME/Settings/RPV130/Enabled",rpv130Communication:"/Equipment/VME/Variables/RPV130/CommunicationOK",
 easirocAddress:"/Equipment/EASIROC/Settings/Network/IPAddress",easirocEnabled:"/Equipment/EASIROC/Settings/Enabled",easirocCommunication:"/Equipment/EASIROC/Variables/RBCPCommunicationOK",easirocTcpReachable:"/Equipment/EASIROC/Variables/TCPReachable",easirocTcpConnected:"/Equipment/EASIROC/Variables/TCPConnected",easirocAcquisitionRunning:"/Equipment/EASIROC/Variables/AcquisitionRunning"
});
const MODULES=Object.freeze([
 {id:"v792",setting:"v792Enabled",link:"/Equipment/VME/Settings/V792"},{id:"v1190",setting:"v1190Enabled",link:"/Equipment/VME/Settings/V1190"},{id:"v1720e",setting:"v1720eEnabled",link:"/Equipment/VME/Settings/V1720E"},{id:"v775",setting:"v775Enabled",link:"/Equipment/VME/Settings/V775"},{id:"rpv130",setting:"rpv130Enabled",link:"/Equipment/VME/Settings/RPV130"},{id:"easiroc",setting:"easirocEnabled",link:"/Equipment/EASIROC/Settings"}
]);
const ODB_PATHS=Object.values(ODB);let mockMode=false,mockScenario="running_ok",mockValues=null,currentValues={},transitionPending=false,transitionTarget=null,lastCanStart=false,lastRunState="—",lastControlRunState="—",lastTransitionInProgress=null,lastAlarmSystemActive=null,alarmTogglePending=false,lastMessageRefresh=0,historyWindowMs=10*60*1000,historyUpdateMs=1000,lastHistorySample=0;const togglePending=new Set(),rateHistory=[];
const element=id=>document.getElementById(id),valid=v=>v!==null&&v!==undefined&&v!=="",truth=v=>v===true||v===1;
function setText(id,value,fallback="—"){element(id).textContent=valid(value)?String(value):fallback}function count(value){const n=Number(value);return valid(value)&&Number.isFinite(n)?Math.trunc(n).toLocaleString("en-US"):"—"}function rate(value){const n=Number(value);return valid(value)&&Number.isFinite(n)?n.toLocaleString("en-US",{maximumFractionDigits:n<10?2:1}):"—"}function dataRate(value){const n=Number(value);return !valid(value)||!Number.isFinite(n)?"—":Math.abs(n)>=1000?`${(n/1000).toFixed(2)} MB/s`:`${n.toFixed(1)} kB/s`}function duration(value){const n=Number(value);return !Number.isFinite(n)||n<0?"—":[Math.floor(n/3600),Math.floor(n%3600/60),Math.floor(n%60)].map(x=>String(x).padStart(2,"0")).join(":")}function runState(value){return Number(value)===1?"STOPPED":Number(value)===2?"PAUSED":Number(value)===3?"RUNNING":"—"}function hex(value){const n=Number(value);return valid(value)&&Number.isFinite(n)?`0x${Math.trunc(n).toString(16).toUpperCase().padStart(8,"0")}`:"—"}
function formatRunTime(value){if(!valid(value))return "—";const source=String(value).trim(),numeric=source.match(/^(\d{4})[-/](\d{1,2})[-/](\d{1,2})[ T](\d{1,2}):(\d{2}):(\d{2})/);if(numeric)return `${numeric[1]}/${numeric[2].padStart(2,"0")}/${numeric[3].padStart(2,"0")} ${numeric[4].padStart(2,"0")}:${numeric[5]}:${numeric[6]}`;const monthNames={Jan:1,Feb:2,Mar:3,Apr:4,May:5,Jun:6,Jul:7,Aug:8,Sep:9,Oct:10,Nov:11,Dec:12},midas=source.match(/^(?:[A-Za-z]{3}\s+)?([A-Za-z]{3})\s+(\d{1,2})\s+(\d{1,2}):(\d{2}):(\d{2})\s+(\d{4})$/);if(midas&&monthNames[midas[1]])return `${midas[6]}/${String(monthNames[midas[1]]).padStart(2,"0")}/${midas[2].padStart(2,"0")} ${midas[3].padStart(2,"0")}:${midas[4]}:${midas[5]}`;const parsed=new Date(source);if(Number.isNaN(parsed.getTime()))return source;const part=number=>String(number).padStart(2,"0");return `${parsed.getFullYear()}/${part(parsed.getMonth()+1)}/${part(parsed.getDate())} ${part(parsed.getHours())}:${part(parsed.getMinutes())}:${part(parsed.getSeconds())}`}
function status(id,label,kind){const node=element(id);node.textContent=label;node.className=`status-badge state-${kind}`}function severityKind(value){return value==="OK"?"ok":value==="WARNING"?"warning":value==="ERROR"?"error":"unknown"}
function moduleStatus(id,enabled,communication){if(!valid(enabled)){status(`${id}-status`,"—","unknown");return}if(!truth(enabled)){status(`${id}-status`,"Disabled","disabled");return}if(truth(communication))status(`${id}-status`,"Comm OK","ok");else status(`${id}-status`,`Disconnected`,"disconnected")}
function frontendStatus(id,connected,participating,severity,running){if(!valid(connected)){status(`${id}-frontend-status`,"—","unknown");return}if(running&&valid(participating)&&!truth(participating)){status(`${id}-frontend-status`,"Not participating","disabled");return}if(!truth(connected)){status(`${id}-frontend-status`,"Disconnected",running?severityKind(severity):"disconnected");return}status(`${id}-frontend-status`,"Connected",severityKind(severity))}
function updateAlarmButton(){
 const button=element("alarm-toggle"),known=valid(lastAlarmSystemActive);
 button.textContent=`Alarm: ${known?(truth(lastAlarmSystemActive)?"ON":"OFF"):"—"}`;
 button.className=`alarm-toggle ${known&&truth(lastAlarmSystemActive)?"alarm-on":"alarm-off"}`;
 button.disabled=!known||alarmTogglePending;
 button.setAttribute("aria-pressed",known?String(truth(lastAlarmSystemActive)):"false");
}
function updateSeverity(severity){element("global-status").className=`global-summary severity-${severityKind(severity)}`}
function renderToggle(module,enabled,locked){const button=element(`${module.id}-toggle`),isEnabled=truth(enabled),pending=togglePending.has(module.id);button.className=`enable-toggle${isEnabled?" enabled":""}${locked?" locked":""}${pending?" busy":""}`;button.disabled=locked||pending||!valid(enabled);button.setAttribute("aria-pressed",String(isEnabled));button.querySelector("b").textContent=locked?(isEnabled?"ENABLED · LOCKED":"DISABLED · LOCKED"):(isEnabled?"ENABLED":"DISABLED")}
function render(values){currentValues=values;const v=name=>values[ODB[name]],run=runState(v("runState")),running=run==="RUNNING",active=run!=="STOPPED"&&run!=="—",scope=running?"(current)":run==="STOPPED"?"(last run)":"(current)";lastRunState=run;lastControlRunState=runState(v("controlRunState"));lastTransitionInProgress=v("transitionInProgress");lastAlarmSystemActive=v("alarmSystemActive");renderLimit("acquisition-time",v("acquisitionTime"));renderEventLimit();updateAlarmButton();if(transitionTarget&&lastControlRunState===transitionTarget&&valid(lastTransitionInProgress)&&Number(lastTransitionInProgress)===0){transitionPending=false;transitionTarget=null}
 updateSeverity(v("globalSeverity"));setText("global-severity",v("globalSeverity"));setText("global-summary",v("globalSummary"),"No summary available");setText("run-number",v("runNumber"));setText("run-state",run);element("run-card").className=`run-card state-card-${run.toLowerCase().replace("—","unknown")}`;setText("start-time",formatRunTime(v("startTime")));setText("stop-time",running?"—":formatRunTime(v("stopTime")));setText("run-duration",duration(v("runDuration")));setText("run-vme-events",count(v("vmeEventCount")));setText("run-easiroc-events",count(v("easirocEventCount")));setText("run-event-slip",count(v("vmeEventSlip")));["start-time-scope","stop-time-scope","duration-scope","vme-events-scope","easiroc-events-scope","event-slip-scope"].forEach(id=>setText(id,scope,""));
 lastCanStart=truth(v("canStart"));if(running){setText("can-start","N/A");setText("can-start-reason","Run in progress")}else if(run==="PAUSED"){setText("can-start","N/A");setText("can-start-reason","Run paused")}else{setText("can-start",lastCanStart?"READY":"BLOCKED");setText("can-start-reason",v("canStartReason"),lastCanStart?"All pre-start checks passed":"No reason available")}updateButtons();
 frontendStatus("vme",v("vmeConnected"),v("vmeParticipating"),v("vmeSeverity"),running);frontendStatus("easiroc",v("easirocConnected"),v("easirocParticipating"),v("easirocSeverity"),running);setText("vme-frontend-events",count(v("vmeEventCount")));setText("easiroc-frontend-events",count(v("easirocEventCount")));["v792","v1190","v1720e","v775","rpv130"].forEach(id=>{setText(`${id}-address`,hex(v(`${id}Address`)));moduleStatus(id,v(`${id}Enabled`),v(`${id}Communication`))});setText("easiroc-address",v("easirocAddress"));let easirocComm=v("easirocCommunication");if(truth(v("easirocEnabled"))&&running)easirocComm=truth(easirocComm)&&truth(v("easirocTcpReachable"))&&truth(v("easirocTcpConnected"))&&truth(v("easirocAcquisitionRunning"));moduleStatus("easiroc",v("easirocEnabled"),easirocComm);MODULES.forEach(module=>renderToggle(module,v(module.setting),active));
 setText("vme-event-rate",rate(v("vmeEventRate")));setText("vme-data-rate",dataRate(v("vmeDataRate")));setText("vme-event-count",count(v("vmeEventCount")));setText("easiroc-event-rate",rate(v("easirocEventRate")));setText("easiroc-data-rate",dataRate(v("easirocDataRate")));setText("easiroc-event-count",count(v("easirocEventCount")));setText("disk-path",v("diskPath"));const filename=v("currentFilename"),parts=valid(filename)?String(filename).split("/"):[];setText("current-filename",parts.length?parts[parts.length-1]:null,"No active file");const free=rate(v("diskFree")),total=rate(v("diskTotal"));setText("disk-capacity",free==="—"?"—":`${free} / ${total} GB`);sampleRates(v("vmeEventRate"),v("easirocEventRate"));setText("last-refresh",`Updated ${new Date().toLocaleTimeString()}`)}
function updateButtons(){
 const upper=element("upper-run-button"),lower=element("lower-run-button"),state=lastControlRunState;
 const stateKnown=state==="STOPPED"||state==="RUNNING"||state==="PAUSED";
 const transitionLocked=transitionPending||!valid(lastTransitionInProgress)||Number(lastTransitionInProgress)!==0;
 upper.textContent=state==="STOPPED"?"START":"STOP";
 upper.className=`run-button ${state==="STOPPED"?"start-button":"stop-button"}`;
 lower.textContent=state==="STOPPED"?"CLEAR":state==="RUNNING"?"PAUSE":"RESUME";
 lower.className="run-button";
 upper.disabled=mockMode||!stateKnown||transitionLocked||(state==="STOPPED"&&!lastCanStart);
 lower.disabled=state==="STOPPED"?false:mockMode||!stateKnown||transitionLocked;
}
function sampleRates(vme,easiroc){const now=Date.now();if(now-lastHistorySample<historyUpdateMs)return;lastHistorySample=now;const v=Number(vme),e=Number(easiroc);rateHistory.push({time:now,vme:Number.isFinite(v)?v:null,easiroc:Number.isFinite(e)?e:null});while(rateHistory.length&&rateHistory[0].time<now-HISTORY_MAX_MS)rateHistory.shift();drawRateHistory()}
function historyDurationLabel(milliseconds){const minutes=milliseconds/60000;if(minutes<60)return `${Math.round(minutes)} min`;if(minutes<1440)return `${Math.round(minutes/60)} hour`;return "1 day"}
function setHistoryWindow(minutes){historyWindowMs=minutes*60*1000;document.querySelectorAll("[data-history-minutes]").forEach(button=>{const active=Number(button.dataset.historyMinutes)===minutes;button.classList.toggle("active",active);button.setAttribute("aria-pressed",String(active))});drawRateHistory()}
function setHistoryUpdate(seconds){historyUpdateMs=seconds*1000;document.querySelectorAll("[data-history-seconds]").forEach(button=>{const active=Number(button.dataset.historySeconds)===seconds;button.classList.toggle("active",active);button.setAttribute("aria-pressed",String(active))})}
function drawRateHistory(){const canvas=element("rate-canvas"),rect=canvas.getBoundingClientRect(),dpr=window.devicePixelRatio||1,w=Math.max(300,Math.round(rect.width)),h=Math.max(200,Math.round(rect.height));if(canvas.width!==w*dpr||canvas.height!==h*dpr){canvas.width=w*dpr;canvas.height=h*dpr}const ctx=canvas.getContext("2d");ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);const css=getComputedStyle(document.documentElement),ink=css.getPropertyValue("--muted").trim(),grid=css.getPropertyValue("--line").trim(),vmeColor=css.getPropertyValue("--graph-vme").trim(),easirocColor=css.getPropertyValue("--graph-easiroc").trim(),left=72,right=18,top=12,bottom=48,pw=w-left-right,ph=h-top-bottom,end=Date.now(),start=end-historyWindowMs,visible=rateHistory.filter(point=>point.time>=start),values=visible.flatMap(point=>[point.vme,point.easiroc]).filter(Number.isFinite),max=values.reduce((maximum,value)=>Math.max(maximum,value),1)*1.1;ctx.font="11px sans-serif";ctx.textBaseline="middle";for(let i=0;i<=4;i++){const y=top+ph*i/4,value=max*(1-i/4);ctx.strokeStyle=grid;ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(w-right,y);ctx.stroke();ctx.fillStyle=ink;ctx.textAlign="right";ctx.fillText(rate(value),left-8,y)}for(let i=0;i<=6;i++){const x=left+pw*i/6,remaining=historyWindowMs*(1-i/6);ctx.strokeStyle=grid;ctx.beginPath();ctx.moveTo(x,top);ctx.lineTo(x,top+ph);ctx.stroke();ctx.fillStyle=ink;ctx.textAlign=i===0?"left":i===6?"right":"center";ctx.fillText(i===6?"now":`-${historyDurationLabel(remaining)}`,x,h-bottom+16)}ctx.strokeStyle=ink;ctx.lineWidth=1.3;ctx.beginPath();ctx.moveTo(left,top);ctx.lineTo(left,top+ph);ctx.lineTo(w-right,top+ph);ctx.stroke();ctx.fillStyle=ink;ctx.font="bold 12px sans-serif";ctx.textAlign="center";ctx.fillText("Time",left+pw/2,h-9);ctx.save();ctx.translate(15,top+ph/2);ctx.rotate(-Math.PI/2);ctx.fillText("Rate [Hz]",0,0);ctx.restore();const step=Math.max(1,Math.ceil(visible.length/Math.max(1,pw)));function line(key,color){ctx.strokeStyle=color;ctx.lineWidth=2;ctx.beginPath();let begun=false;visible.forEach((point,index)=>{if(index%step!==0&&index!==visible.length-1)return;if(!Number.isFinite(point[key])){begun=false;return}const x=left+(point.time-start)/historyWindowMs*pw,y=top+ph-point[key]/max*ph;if(!begun){ctx.moveTo(x,y);begun=true}else ctx.lineTo(x,y)});ctx.stroke()}line("vme",vmeColor);line("easiroc",easirocColor)}
function valuesFromRpc(rpc){const values={};ODB_PATHS.forEach((path,index)=>values[path]=rpc.result.status[index]===MIDAS_SUCCESS?rpc.result.data[index]:null);return values}function showError(message){setText("connection-error",message);element("connection-error").hidden=false}function clearError(){element("connection-error").hidden=true}
async function refreshMessages(){try{const rpc=await mjsonrpc_call("cm_msg_retrieve",{facility:"midas",time:0,min_messages:30});renderMessages((rpc.result.messages||"").split("\n").filter(Boolean).slice(0,30))}catch(error){renderMessages([`Messages unavailable: ${decodeError(error)}`])}}
function messageKind(line){return /,ERROR\]/i.test(line)||/\bERROR\b/i.test(line)?"error":/,WARNING\]/i.test(line)||/\bWARNING\b/i.test(line)?"warning":"info"}function renderMessages(messages){const list=element("message-list");list.replaceChildren();(messages.length?messages:["No recent MIDAS messages."]).forEach(line=>{const p=document.createElement("p");p.className=`message-${messageKind(line)}`;p.textContent=line;list.appendChild(p)})}
function decodeError(error){return typeof mjsonrpc_decode_error==="function"?mjsonrpc_decode_error(error):String(error)}
const LIMITS=Object.freeze({
 "acquisition-time":{key:"acquisitionTime",label:"Acquisition Time",max:4294967295},
 "event-limit":{label:"Event Limit",max:4294967295}
});
function renderLimit(id,value,enabled=true){
 const input=element(id);
 if(input.dataset.pending==="1")return;
 // MIDAS JSON-RPC returns TID_UINT32 values such as Run duration as hex text.
 const displayValue=(id==="acquisition-time"||id==="event-limit")&&valid(value)?Number(value):value;
 const available=valid(value)&&
  (Number.isSafeInteger(displayValue)&&displayValue>=0&&displayValue<=4294967295);
 input.disabled=!enabled||!available;
 if(document.activeElement!==input)input.value=available?String(displayValue):"";
}
function renderEventLimit(force=false){
 const input=element("event-limit"),warning=element("event-limit-warning");
 if(input.dataset.pending==="1")return;
 const vme=currentValues[ODB.vmeEventLimit],easiroc=currentValues[ODB.easirocEventLimit];
 const available=[vme,easiroc].every(value=>valid(value)&&Number.isSafeInteger(Number(value))&&Number(value)>=0&&Number(value)<=LIMITS["event-limit"].max);
 const matches=available&&Number(vme)===Number(easiroc);
 input.disabled=!available;
 warning.hidden=matches;
 warning.textContent=available?"VME/EASIROC limits differ":"VME/EASIROC limits unavailable";
 if(force||document.activeElement!==input)input.value=matches?String(Number(vme)):"";
}
async function saveLimit(id){
 const input=element(id),config=LIMITS[id],path=config.key?ODB[config.key]:null,raw=input.value.trim();
 if(input.dataset.pending==="1"||input.disabled)return;
 const value=raw===""?0:Number(raw);
 if(input.validity.badInput||!Number.isSafeInteger(value)||value<0||value>config.max||(!input.validity.valid&&raw!=="")){
  input.setCustomValidity(`Enter a whole number from 0 to ${config.max}`);
  input.reportValidity();
  return;
 }
 input.setCustomValidity("");
 input.dataset.pending="1";
 input.disabled=true;
 try{
  if(id==="event-limit"){
   const paths=[ODB.vmeEventLimit,ODB.easirocEventLimit];
   if(mockMode){paths.forEach(key=>{mockValues[key]=value;currentValues[key]=value});return}
   const write=await mjsonrpc_db_paste(paths,[value,value]);
   const readback=await mjsonrpc_db_get_values(paths);
   paths.forEach((key,index)=>{currentValues[key]=readback.result.status[index]===MIDAS_SUCCESS?readback.result.data[index]:null});
   if(write.result.status.some(status=>status!==MIDAS_SUCCESS)||
      readback.result.status.some(status=>status!==MIDAS_SUCCESS)||
      paths.some(key=>Number(currentValues[key])!==value))
    throw new Error("VME/EASIROC write or readback did not match requested value");
  }else{
   if(mockMode){mockValues[path]=value;currentValues[path]=value;return}
   const write=await mjsonrpc_db_set_value(path,value);
   if(write.result.status[0]!==MIDAS_SUCCESS)throw new Error(`ODB write status ${write.result.status[0]}`);
   const readback=await mjsonrpc_db_get_values([path]);
   if(readback.result.status[0]!==MIDAS_SUCCESS||Number(readback.result.data[0])!==value)
    throw new Error("ODB readback did not match requested value");
   currentValues[path]=readback.result.data[0];
  }
  clearError();
 }catch(error){showError(`Cannot change ${config.label}: ${decodeError(error)}`)}
 finally{
  delete input.dataset.pending;
  if(id==="event-limit")renderEventLimit(true);else renderLimit(id,currentValues[path]);
 }
}
async function refreshDashboard(){try{render(valuesFromRpc(await mjsonrpc_db_get_values(ODB_PATHS)));clearError();if(Date.now()-lastMessageRefresh>=MESSAGE_INTERVAL_MS){lastMessageRefresh=Date.now();refreshMessages()}}catch(error){lastControlRunState="—";lastTransitionInProgress=null;lastAlarmSystemActive=null;updateAlarmButton();updateButtons();showError(`Cannot refresh MIDAS status: ${decodeError(error)}`)}finally{window.setTimeout(refreshDashboard,REFRESH_INTERVAL_MS)}}
async function setModuleEnabled(module){if(lastRunState!=="STOPPED"||togglePending.has(module.id))return;const path=ODB[module.setting],next=!truth(currentValues[path]);togglePending.add(module.id);renderToggle(module,currentValues[path],false);try{if(mockMode){mockValues[path]=next;render(mockValues);return}const write=await mjsonrpc_db_set_value(path,next);if(write.result.status[0]!==MIDAS_SUCCESS)throw new Error(`ODB write status ${write.result.status[0]}`);const readback=await mjsonrpc_db_get_values([path]);if(readback.result.status[0]!==MIDAS_SUCCESS||truth(readback.result.data[0])!==next)throw new Error("ODB readback did not match requested value");currentValues[path]=readback.result.data[0];render(currentValues);clearError()}catch(error){showError(`Cannot change ${module.id} Enable: ${decodeError(error)}`)}finally{togglePending.delete(module.id);renderToggle(module,currentValues[path],lastRunState!=="STOPPED")}}
async function toggleAlarmSystem(){
 if(alarmTogglePending||!valid(lastAlarmSystemActive))return;
 const path=ODB.alarmSystemActive,next=!truth(lastAlarmSystemActive);
 if(mockMode){mockValues[path]=next;render(mockValues);return}
 alarmTogglePending=true;
 updateAlarmButton();
 try{
  const write=await mjsonrpc_db_set_value(path,next);
  if(write.result.status[0]!==MIDAS_SUCCESS)throw new Error(`ODB write status ${write.result.status[0]}`);
  const readback=await mjsonrpc_db_get_values([path]);
  if(readback.result.status[0]!==MIDAS_SUCCESS||truth(readback.result.data[0])!==next)
   throw new Error("Alarm system ODB readback did not match requested value");
  lastAlarmSystemActive=readback.result.data[0];
  currentValues[path]=lastAlarmSystemActive;
  if(next)mhttpd_alarm_snooze_clear();
  else mhttpd_alarm_snooze(0);
  clearError();
 }catch(error){
  showError(`Cannot change MIDAS Alarm: ${decodeError(error)}`);
 }finally{
  alarmTogglePending=false;
  updateAlarmButton();
 }
}
async function requestTransition(transition){const state=lastControlRunState,allowed=transition==="TR_START"?state==="STOPPED"&&lastCanStart:transition==="TR_STOP"?state==="RUNNING"||state==="PAUSED":transition==="TR_PAUSE"?state==="RUNNING":transition==="TR_RESUME"&&state==="PAUSED";if(mockMode||transitionPending||!allowed||!valid(lastTransitionInProgress)||Number(lastTransitionInProgress)!==0)return;const target={TR_START:"RUNNING",TR_STOP:"STOPPED",TR_PAUSE:"PAUSED",TR_RESUME:"RUNNING"}[transition],action={TR_START:"Starting",TR_STOP:"Stopping",TR_PAUSE:"Pausing",TR_RESUME:"Resuming"}[transition];transitionPending=true;updateButtons();setText("transition-status",`${action} run…`);try{const rpc=await mjsonrpc_call("cm_transition",{transition});if(rpc.result.status!==MIDAS_SUCCESS)throw new Error(rpc.result.error_string||`MIDAS transition status ${rpc.result.status}`);transitionTarget=target;setText("transition-status",`${action} run requested`)}catch(error){transitionPending=false;transitionTarget=null;showError(`Transition failed: ${decodeError(error)}`);setText("transition-status","Transition failed")}finally{updateButtons()}}
function requestManualBufferClear(){window.location.assign(mockMode?"buffer-clear.html?mock=1":"/buffer-clear.html")}
function seedMockHistory(){if(rateHistory.length>1)return;const now=Date.now(),vme=Number(mockValues[ODB.vmeEventRate])||0,easiroc=Number(mockValues[ODB.easirocEventRate])||0;rateHistory.length=0;for(let i=1440;i>0;i--)rateHistory.push({time:now-i*60000,vme:Math.max(0,vme+Math.sin(i/17)*55),easiroc:Math.max(0,easiroc+Math.cos(i/23)*42)})}
function selectMockScenario(name){if(!window.DAQ_MOCK_SCENARIOS[name])return;mockScenario=name;mockValues={...window.DAQ_MOCK_SCENARIOS[name]};mockValues[ODB.controlRunState]=mockValues[ODB.runState];mockValues[ODB.transitionInProgress]=0;mockValues[ODB.alarmSystemActive]=true;mockValues[ODB.acquisitionTime]=currentValues[ODB.acquisitionTime]??0;mockValues[ODB.vmeEventLimit]=currentValues[ODB.vmeEventLimit]??0;mockValues[ODB.easirocEventLimit]=currentValues[ODB.easirocEventLimit]??0;document.querySelectorAll("[data-mock-scenario]").forEach(button=>button.classList.toggle("active",button.dataset.mockScenario===name));seedMockHistory();render(mockValues);renderMessages(window.DAQ_MOCK_MESSAGES)}
function mockRefresh(){if(!mockMode)return;const running=runState(mockValues[ODB.runState])==="RUNNING",phase=Date.now()/3500;if(running){mockValues[ODB.vmeEventRate]=1248+55*Math.sin(phase);mockValues[ODB.easirocEventRate]=1244+42*Math.cos(phase*.9);mockValues[ODB.runDuration]=Number(mockValues[ODB.runDuration])+1}render(mockValues);window.setTimeout(mockRefresh,REFRESH_INTERVAL_MS)}
function setupLinksAndEvents(){MODULES.forEach(module=>{element(`${module.id}-link`).href=`?cmd=odb&odb_path=${encodeURIComponent(module.link)}`;element(`${module.id}-toggle`).addEventListener("click",()=>setModuleEnabled(module))});element("upper-run-button").addEventListener("click",()=>requestTransition(lastControlRunState==="STOPPED"?"TR_START":"TR_STOP"));element("lower-run-button").addEventListener("click",()=>{if(lastControlRunState==="STOPPED")requestManualBufferClear();else if(lastControlRunState==="RUNNING")requestTransition("TR_PAUSE");else if(lastControlRunState==="PAUSED")requestTransition("TR_RESUME")});element("alarm-toggle").addEventListener("click",toggleAlarmSystem);["acquisition-time","event-limit"].forEach(id=>{element(id).addEventListener("change",()=>saveLimit(id));element(id).addEventListener("input",event=>event.target.setCustomValidity(""))});document.querySelectorAll("[data-mock-scenario]").forEach(button=>button.addEventListener("click",()=>selectMockScenario(button.dataset.mockScenario)));document.querySelectorAll("[data-history-minutes]").forEach(button=>button.addEventListener("click",()=>setHistoryWindow(Number(button.dataset.historyMinutes))));document.querySelectorAll("[data-history-seconds]").forEach(button=>button.addEventListener("click",()=>setHistoryUpdate(Number(button.dataset.historySeconds))));window.addEventListener("resize",drawRateHistory)}
function initializeDashboard(){mhttpdConfigSet("speakTalk",false);mhttpdConfigSet("alarmSound",true);updateAlarmButton();setupLinksAndEvents();setHistoryWindow(10);setHistoryUpdate(1);const query=new URLSearchParams(window.location.search);mockMode=query.has("mock");if(mockMode){document.body.classList.add("mock-mode");element("mock-notice").hidden=false;const requested=query.get("mock"),scenario=requested==="stopped"?"stopped_warning":requested==="error"?"running_error":"running_ok";selectMockScenario(scenario);window.setTimeout(mockRefresh,REFRESH_INTERVAL_MS);return}mhttpd_init("DAQ",REFRESH_INTERVAL_MS);refreshDashboard()}
