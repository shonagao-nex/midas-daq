"use strict";

const MIDAS_SUCCESS = 1;
const STOPPED = 1;
const REFRESH_MS = 1000;
const ACK_TIMEOUT_MS = 35000;
const TERMINAL = new Set(["Succeeded", "Failed", "Rejected", "Indeterminate"]);
const PATHS = Object.freeze({
  run: "/Runinfo/State",
  vme: {
    request: "/Equipment/VME/Commands/BufferClearRequestId",
    active: "/Equipment/VME/Variables/BufferClear/ActiveRequestId",
    handled: "/Equipment/VME/Variables/BufferClear/LastHandledRequestId",
    successful: "/Equipment/VME/Variables/BufferClear/LastSuccessfulRequestId",
    state: "/Equipment/VME/Variables/BufferClear/State",
    inProgress: "/Equipment/VME/Variables/BufferClear/InProgress",
    time: "/Equipment/VME/Variables/BufferClear/LastClearUnixTime",
    error: "/Equipment/VME/Variables/BufferClear/LastError",
    v792: "/Equipment/VME/Variables/BufferClear/V792Result",
    v1190: "/Equipment/VME/Variables/BufferClear/V1190Result",
    v775: "/Equipment/VME/Variables/BufferClear/V775Result",
    v1720e: "/Equipment/VME/Variables/BufferClear/V1720EResult"
  },
  eas: {
    request: "/Equipment/EASIROC/Commands/BufferClearRequestId",
    active: "/Equipment/EASIROC/Variables/BufferClear/ActiveRequestId",
    handled: "/Equipment/EASIROC/Variables/BufferClear/LastHandledRequestId",
    successful: "/Equipment/EASIROC/Variables/BufferClear/LastSuccessfulRequestId",
    state: "/Equipment/EASIROC/Variables/BufferClear/State",
    inProgress: "/Equipment/EASIROC/Variables/BufferClear/InProgress",
    time: "/Equipment/EASIROC/Variables/BufferClear/LastClearUnixTime",
    error: "/Equipment/EASIROC/Variables/BufferClear/LastError",
    bytes: "/Equipment/EASIROC/Variables/BufferClear/DrainedBytes",
    result: "/Equipment/EASIROC/Variables/BufferClear/NIMEASIROCResult"
  }
});
const state = {mock:false, values:{}, clients:{fevme:false,feeasiroc:false}, busy:{vme:false,eas:false}};
const el = id => document.getElementById(id);
const truth = value => value === true || value === 1 || value === "y";
const u32 = value => { const n=Number(value); return Number.isFinite(n)&&n>=0&&n<=0xffffffff?Math.floor(n):0; };
const text = value => value === undefined || value === null || value === "" ? "—" : String(value);
function allPaths(){return [PATHS.run,...Object.values(PATHS.vme),...Object.values(PATHS.eas)];}
function value(group,key){return state.values[PATHS[group][key]];}
function timeText(value){const n=Number(value);return n>0?new Date(n*1000).toLocaleString():"—";}
function runName(){return Number(state.values[PATHS.run])===1?"STOPPED":Number(state.values[PATHS.run])===2?"PAUSED":Number(state.values[PATHS.run])===3?"RUNNING":"UNKNOWN";}
function show(kind,message){el("message").className=`message ${kind}`;el("message").textContent=message;el("message").hidden=false;}
function update(){
  const stopped=Number(state.values[PATHS.run])===STOPPED; el("run-state").textContent=runName();
  [["vme","fevme"],["eas","feeasiroc"]].forEach(([group,client])=>{el(`${group}-frontend`).textContent=state.clients[client]?"Running":"Not running";el(`${group}-state`).textContent=text(value(group,"state"));el(`${group}-request`).textContent=text(value(group,"handled"));el(`${group}-time`).textContent=timeText(value(group,"time"));el(`${group}-error`).textContent=text(value(group,"error"));});
  el("vme-v792").textContent=text(value("vme","v792"));el("vme-v1190").textContent=text(value("vme","v1190"));el("vme-v775").textContent=text(value("vme","v775"));el("vme-v1720e").textContent=text(value("vme","v1720e"));
  el("eas-bytes").textContent=text(value("eas","bytes"));el("eas-result").textContent=text(value("eas","result"));
  el("clear-vme").disabled=!stopped||!state.clients.fevme||truth(value("vme","inProgress"))||state.busy.vme;
  el("clear-eas").disabled=!stopped||!state.clients.feeasiroc||truth(value("eas","inProgress"))||state.busy.eas;
  el("availability").textContent=!stopped?"Clear is locked unless Run state is STOPPED.":"Only a running corresponding frontend can accept a clear request.";
}
async function readStatus(){
  if(state.mock){const mock=window.BUFFER_CLEAR_MOCK;state.values[PATHS.run]=mock.runState;state.clients={...mock.clients};for(const group of ["vme","eas"])for(const [key,path] of Object.entries(PATHS[group]))state.values[path]=mock[group][key];return;}
  const paths=allPaths(),rpc=await mjsonrpc_db_get_values(paths);paths.forEach((path,index)=>{if(rpc.result.status[index]===MIDAS_SUCCESS)state.values[path]=rpc.result.data[index];});
  const [vme,eas]=await Promise.all([mjsonrpc_cm_exist("fevme",true),mjsonrpc_cm_exist("feeasiroc",true)]);state.clients.fevme=vme.result.status===MIDAS_SUCCESS;state.clients.feeasiroc=eas.result.status===MIDAS_SUCCESS;
}
function nextId(group){const highest=Math.max(u32(value(group,"request")),u32(value(group,"active")),u32(value(group,"handled")),u32(value(group,"successful")));if(highest>=0xffffffff)throw new Error("RequestId exhausted; DWORD wraparound is intentionally refused");return highest+1;}
async function waitAck(group,id){const deadline=Date.now()+ACK_TIMEOUT_MS;while(Date.now()<deadline){await readStatus();update();if(u32(value(group,"handled"))>=id&&u32(value(group,"active"))===0&&TERMINAL.has(String(value(group,"state"))))return String(value(group,"state"));await new Promise(resolve=>setTimeout(resolve,500));}throw new Error("Frontend did not acknowledge the request; it may have stopped after the request was submitted");}
async function requestClear(group){
  if(state.busy[group])return;const client=group==="vme"?"fevme":"feeasiroc";try{await readStatus();update();if(Number(state.values[PATHS.run])!==STOPPED)throw new Error(`Run state is ${runName()}, not STOPPED`);if(!state.clients[client])throw new Error(`${client} is not running; start it from the Programs page`);if(truth(value(group,"inProgress")))throw new Error("Another clear request is in progress");if(!window.confirm(`Discard buffered ${group==="vme"?"VME":"NIM-EASIROC"} event data?`))return;const id=nextId(group);state.busy[group]=true;update();if(state.mock){state.values[PATHS[group].handled]=id;state.values[PATHS[group].successful]=id;state.values[PATHS[group].state]="Succeeded";show("success",`Mock request ${id} succeeded; no MIDAS or hardware access occurred.`);return;}const rpc=await mjsonrpc_db_set_value(PATHS[group].request,id);if(rpc.result.status[0]!==MIDAS_SUCCESS)throw new Error("ODB request write failed");const terminal=await waitAck(group,id);show(terminal==="Succeeded"?"success":"warning",`${client} acknowledged request ${id}: ${terminal}.`);}catch(error){show("error",typeof mjsonrpc_decode_error==="function"?mjsonrpc_decode_error(error):String(error));}finally{state.busy[group]=false;update();}}
async function refresh(){try{await readStatus();update();}catch(error){show("error",`Status refresh failed: ${typeof mjsonrpc_decode_error==="function"?mjsonrpc_decode_error(error):String(error)}`);}}
function initializeBufferClearPage(){state.mock=new URLSearchParams(location.search).has("mock");el("clear-vme").addEventListener("click",()=>requestClear("vme"));el("clear-eas").addEventListener("click",()=>requestClear("eas"));if(state.mock){document.body.classList.add("mock-mode");el("mock-panel").hidden=false;}else mhttpd_init("Manual Buffer Clear",REFRESH_MS);refresh();window.setInterval(refresh,REFRESH_MS);}
