/* Run with Node.js; no MIDAS, browser service, socket or hardware dependency. */
"use strict";
const fs = require("node:fs"), vm = require("node:vm"), assert = require("node:assert/strict");
const path = require("node:path");
const web = path.resolve(__dirname, "../../daq_monitor/web");
const html = fs.readFileSync(path.join(web,"easiroc.html"),"utf8");
const nodes = new Map(), all = [];
class Node {
  constructor(tag="div") {this.tagName=tag;this.children=[];this.listeners={};this.value="";this.type="";this.disabled=false;this.classList={add(){},toggle(){}};all.push(this);}
  set id(value) {this._id=value;nodes.set(value,this);} get id(){return this._id;}
  get options(){return this.children.filter(n=>n.tagName==="option");}
  append(...items){this.children.push(...items);} appendChild(item){this.append(item);}
  replaceChildren(...items){this.children=items;}
  addEventListener(name,fn){this.listeners[name]=fn;}
}
for (const match of html.matchAll(/<(\w+)[^>]*\bid="([^"]+)"[^>]*>/g)) new Node(match[1]).id=match[2];
let rpcCalls=0;
const document={getElementById(id){return nodes.get(id)||null;},createElement(tag){return new Node(tag);},createTextNode(text){return {textContent:text};},
  querySelectorAll(selector){return selector.includes("input")?all.filter(n=>n.tagName==="input"||n.tagName==="select"):[];},body:new Node("body")};
const context=vm.createContext({document,console,URLSearchParams,window:{location:{search:"?mock=match"},setTimeout(fn,ms){if(ms===10000 || ms>40000 || fn.name==="refresh")return;fn();},clearTimeout(){}},
  mjsonrpc_db_get_values(){++rpcCalls;throw Error("Unexpected real ODB read");},mjsonrpc_db_set_value(){++rpcCalls;throw Error("Unexpected real ODB write");}});
vm.runInContext(fs.readFileSync(path.join(web,"easiroc-mock-data.js"),"utf8"),context);
vm.runInContext(fs.readFileSync(path.join(web,"easiroc.js"),"utf8"),context);
const run=code=>vm.runInContext(code,context);
let checks=0;
function check(code,message){assert.equal(run(code),true,message);++checks;}
(async()=>{
  run('state.mock=true;setupEvents();selectMockScenario("match")');
  check('[...state.staged.asic1.input,...state.staged.asic2.input].length===64 && [...state.staged.asic1.input,...state.staged.asic2.input].every(value=>value===480)',"all 64 GUI defaults must be 480");
  check('state.loaded.asic1.code===600 && state.staged.extras.hitThreshold===1',"default complete settings");
  check('element("apply-button").disabled===false',"mock safe apply available");
  run('state.staged.asic1.code=610;updateUi()');
  check('element("apply-button").disabled && unsaved()',"unsaved Apply disabled");
  const oldRequest=run('statusValue("request")');
  await run('applySettings()');
  assert.equal(run('statusValue("request")'),oldRequest);++checks;
  check('state.staged.asic1.code===610',"rejected Apply keeps Desired");
  run('element("ip-address").value="192.168.10.99"');
  await run('readSelectedIp()');
  check('state.staged.asic1.code===610 && state.selected.id==="moduleA" && element("message").className.includes("error")',"unregistered READ preserves Desired and selection");
  await run('saveSettings()');
  check('!unsaved() && state.loaded.asic1.code===610',"Save readback matches");
  assert.equal(run('statusValue("request")'),oldRequest);++checks;
  assert.equal(rpcCalls,0,"Save must not access hardware/MIDAS in mock");++checks;
  run('element("ip-address").value="192.168.10.41"');await run('readSelectedIp()');
  check('state.selected.id==="moduleB" && state.staged.asic1.code===700',"READ different device");
  run('state.staged.extras.probe2Enabled=true;state.staged.extras.probe2Signal=2;state.staged.extras.oneChannel=63;state.staged.extras.andChannels[63]=true;state.staged.extras.triggerWidth=278');
  await run('saveSettings()');
  check('!unsaved() && state.values[ODB.triggerWidth]===278 && state.values[ODB.andChannels][63]===true',"Save extended arrays and selections");
  await run('applySettings()');
  check('statusValue("state")==="Succeeded" && state.values[`${state.selected.base}/Variables/ASICSlowControl/LastApplied/TriggerWidth`]===278',"mock LastApplied contains extras");
  run('element("ip-address").value="192.168.10.40"');await run('readSelectedIp()');
  check('state.staged.asic1.code===610 && state.staged.extras.triggerWidth===118 && !state.staged.extras.andChannels[63]',"device Settings isolation");
  check('state.values["/Equipment/EASIROC/Devices/moduleB/Variables/ASICSlowControl/LastApplied/TriggerWidth"]===278',"device LastApplied isolation");
  run('state.values[DEVICE_ROOT].moduleB.IPAddress="192.168.10.40"');await run('readSelectedIp()');
  check('element("message").textContent.includes("Duplicate IP") && state.staged.asic1.code===610',"reject duplicate IP without changing Desired");
  run('state.values[DEVICE_ROOT].moduleB.IPAddress="192.168.10.41";state.staged.extras.probe1Channel=32;updateUi()');
  check('element("save-button").disabled && element("apply-button").disabled',"invalid Probe blocks both actions");
  run('state.staged.extras.probe1Channel=0;state.staged.asic1.input[0]=255;updateUi()');
  check('validateSettings(state.staged).includes("256..511")',"InputDAC lower bound");
  run('state.staged.asic1.input[0]=480;state.values[ODB.runState]=3;updateUi()');
  check('element("apply-button").disabled && element("save-button").disabled',"RUNNING blocks editing/apply");
  run('state.values[ODB.runState]=1;state.values[ODB.transition]=1;updateUi()');
  check('element("apply-button").disabled',"transition blocks Apply");
  run('state.values[ODB.transition]=null;updateUi()');
  // Null must be treated as unknown, not as numeric zero.
  check('element("apply-button").disabled',"unknown transition blocks Apply");
  run('state.values[ODB.transition]=0;state.mock=false;updateUi()');
  check('!element("apply-button").disabled',"real Apply enabled without HV monitor");
  run('state.mock=true;state.values[ODB.asic1Code]=620');await run('refresh()');
  check('state.staged.asic1.code===610 && state.loaded.asic1.code===620 && unsaved()',"poll preserves Desired when ODB changes");
  run(`state.values[DEVICE_ROOT]={"modulea/name":"moduleA",modulea:{"ipaddress/name":"IPAddress",ipaddress:"192.168.10.40"},"moduleb/name":"moduleB",moduleb:{ipaddress:"192.168.10.41"}}`);
  await run('readSelectedIp()');
  check('state.selected.id==="moduleA"',"MIDAS lowercase/name metadata maps to original device ID");
  run('state.values[DEVICE_ROOT]={moduleA:{IPAddress:"192.168.10.40"},moduleB:{IPAddress:"192.168.10.41"}}');
  run(`
    var writtenPaths=[];
    mjsonrpc_db_get_values=async paths=>({result:{status:paths.map(path=>present(state.values[path])?1:0),data:paths.map(path=>clone(state.values[path]??null))}});
    mjsonrpc_db_set_value=async (path,value)=>{writtenPaths.push(path);state.values[path]=clone(value);return {result:{status:[1]}};};
    state.mock=false;state.staged.asic1.code=630;
  `);
  await run('saveSettings()');
  check('!unsaved() && state.loaded.asic1.code===630 && writtenPaths.length===27 && writtenPaths.every(path=>path.includes("/Settings/"))',"real Save RPC path only writes Settings");
  check('writtenPaths.every(path=>!path.includes("Commands")&&!path.includes("LastApplied"))',"Save never requests hardware or overwrites LastApplied");
  run(`
    mjsonrpc_db_set_value=async (path,value)=>{
      writtenPaths.push(path);state.values[path]=clone(value);
      if(path===ODB.request){
        state.values[ODB.handled]=value;state.values[ODB.successful]=value;
        state.values[ODB.active]=0;state.values[ODB.inProgress]=false;state.values[ODB.state]="Succeeded";
      }
      return {result:{status:[1]}};
    };
  `);
  for (const hv of ["ON","OFF","Unknown","NotImplemented"]) {
    context.hvScenario=hv;
    run('state.values[`${state.selected.base}/HV/State`]=hvScenario;writtenPaths=[];updateUi()');
    check('!element("apply-button").disabled',`HV ${hv} allows UI Apply`);
    await run('applySettings()');
    check('writtenPaths.length===1 && writtenPaths[0]===ODB.request && !state.applyPending',`HV ${hv} posts existing request mailbox`);
  }
  for (const [runValue,transition] of [[3,0],[2,0],[null,0],[1,1],[1,null]]) {
    context.testRun=runValue;context.testTransition=transition;
    run('state.values[ODB.runState]=testRun;state.values[ODB.transition]=testTransition;writtenPaths=[];updateUi()');
    check('element("apply-button").disabled',"unsafe state disables Apply");
    await run('applySettings()');
    check('writtenPaths.length===0',"unsafe state does not post a request");
  }
  run('state.values[ODB.runState]=1;state.values[ODB.transition]=0;writtenPaths=[]');
  // START races the user click after cached STOPPED was rendered.
  run(`
    mjsonrpc_db_get_values=async paths=>{
      if(paths.includes(ODB.transition))state.values[ODB.transition]=1;
      return {result:{status:paths.map(path=>present(state.values[path])?1:0),data:paths.map(path=>clone(state.values[path]??null))}};
    };
  `);
  await run('applySettings()');
  check('writtenPaths.length===0 && element("message").textContent.includes("Transition")',"fresh transition check catches START race before request");
  run(`
    state.values[ODB.transition]=0;
    mjsonrpc_db_get_values=async paths=>({result:{status:paths.map(path=>present(state.values[path])?1:0),data:paths.map(path=>clone(state.values[path]??null))}});
  `);
  run(`
    var failOnce=true;
    mjsonrpc_db_set_value=async (path,value)=>{if(failOnce){failOnce=false;return {result:{status:[0]}};}state.values[path]=clone(value);return {result:{status:[1]}};};
    state.staged.asic1.code=640;
  `);
  await run('saveSettings()');
  check('state.staged.asic1.code===640 && state.loaded.asic1.code===630 && unsaved() && element("message").textContent.includes("Save failed")',"Save error preserves Desired and reconciles stored Settings");
  run('state.values[DEVICE_ROOT].moduleA.IPAddress="192.168.10.42"');
  await run('saveSettings()');
  check('state.staged.asic1.code===640 && element("message").textContent.includes("registry changed")',"registry changes prevent stale-target Save");
  run('state.values[DEVICE_ROOT].moduleA.IPAddress="192.168.10.40";state.mock=true;element("ip-address").value="192.168.10.39"');
  await run('readSelectedIp()');
  check('state.selected.id==="legacy" && !state.extended',"old ASIC-only legacy schema remains usable");
  run('EXTRA_FIELDS.forEach(([key,path])=>state.values[`/Equipment/EASIROC/Settings/${path}`]=clone(EXTRA_DEFAULTS[key]))');
  await run('readSelectedIp()');
  check('state.extended && SETTINGS_PATHS.length===27 && !element("extra-probe1Enabled").disabled',"complete legacy extended schema enables all Apply fields");
  assert.equal(rpcCalls,0);++checks;
  const layoutIds = ['ip-address', 'selected-device', 'run-state', 'save-button', 'apply-button', 'message'];
  // Use the settings heading because its controls are generated dynamically.
  const positions = layoutIds.map(id=>html.indexOf(`id="${id}"`));
  positions.splice(3,0,html.indexOf('Desired Settings'));
  assert.ok(positions.every((position,index)=>position>=0 && (!index || position>positions[index-1])),"IP, selection/DAQ, Desired, Save/Apply, result order");++checks;
  assert.ok(!html.includes("Manual Apply Status") && !html.includes("Settings versus LastApplied") && html.includes("→"));++checks;

  // Drive the real Web request/wait code with an offline ODB and virtual clock.
  // Unlike the lightweight preview, this covers unresolved RPC promises too.
  let now=0, timerId=0;
  const timers=new Map();
  context.Date=class extends Date {static now(){return now;}};
  context.window.setTimeout=(fn,ms)=>{if(fn.name==="refresh")return;const id=++timerId;timers.set(id,{fn,at:now+ms});return id;};
  context.window.clearTimeout=id=>timers.delete(id);
  async function finish(promise) {
    let done=false, failure;
    promise.then(()=>{done=true;},error=>{failure=error;done=true;});
    for(let step=0;step<300 && !done;++step){
      for(let tick=0;tick<50;++tick)await Promise.resolve();
      if(done)break;
      const next=[...timers.entries()].sort((a,b)=>a[1].at-b[1].at)[0];
      if(!next)continue; // A chain of already-resolved preflight promises.
      timers.delete(next[0]);now=next[1].at;next[1].fn();
    }
    assert.ok(done,"Web operation settled without network or real-time waiting");
    if(failure)throw failure;
  }
  function reset() {
    timers.clear();
    run(`state.mock=true;selectMockScenario("match");state.mock=false;state.unconfirmedRequests={};writtenPaths=[];
      mjsonrpc_db_get_values=async paths=>({result:{status:paths.map(path=>present(state.values[path])?1:0),data:paths.map(path=>clone(state.values[path]??null))}});`);
  }
  for (const outcome of ["Succeeded","Failed","RollbackFailed","Indeterminate"]) {
    reset();context.outcome=outcome;
    run(`var oldLast=JSON.stringify(state.values[ODB.lastAsic1Input]);
      mjsonrpc_db_set_value=async(path,value)=>{
        writtenPaths.push(path);state.values[path]=value;
        state.values[ODB.handled]=value;state.values[ODB.active]=0;state.values[ODB.inProgress]=false;state.values[ODB.state]=outcome==="RollbackFailed"?"Failed":outcome;
        state.values[ODB.indeterminate]=outcome==="Indeterminate"||outcome==="RollbackFailed";
        state.values[ODB.applyError]=outcome==="Succeeded"?"":"write failed; Rollback "+(outcome==="Failed"?"succeeded":"failed; HardwareStateIndeterminate");
        return {result:{status:[1]}};
      };`);
    await finish(run('applySettings()'));
    check('!state.applyPending && element("settings-lock").hidden && !element("save-button").disabled',`${outcome} restores editing`);
    check('!element("apply-button").disabled && state.staged.asic1.code===600',`${outcome} confirmed completion permits correction/retry`);
    if(outcome!=="Succeeded") {
      check('element("message").textContent.includes("Rollback") && JSON.stringify(state.values[ODB.lastAsic1Input])===oldLast',"failure displays rollback error and preserves LastApplied");
    }
    if(outcome==="Indeterminate"||outcome==="RollbackFailed")check('element("selected-status").textContent.includes("START inhibited")',"rollback failure preserves START-inhibit indication");
  }
  reset();run('state.values[ODB.clients]={};updateUi()');
  check('element("apply-button").disabled && element("settings-lock").hidden',"absent frontend blocks Apply, not editing");
  await finish(run('applySettings()'));
  check('!writtenPaths.length && !state.applyPending && element("message").textContent.includes("not registered")',"absent frontend sends no request and releases editing");
  run('state.values[ODB.request]=3;state.values[ODB.handled]=1;state.values[ODB.state]="Idle";updateUi()');
  check('element("apply-button").disabled && element("settings-lock").hidden && element("selected-status").textContent.includes("not registered")',"observed development orphaned mailbox stays blocked");

  reset();run('mjsonrpc_db_set_value=()=>new Promise(()=>{})');
  await finish(run('applySettings()'));
  check('!state.applyPending && element("settings-lock").hidden && element("apply-button").disabled && element("message").textContent.includes("timed out")',"hung submission releases editing and blocks ambiguous retry");
  await finish(run('refresh()'));
  check('!state.statusUnavailable && element("apply-button").disabled',"idle snapshot cannot clear an unacknowledged ambiguous submission");
  run('state.values[ODB.handled]=state.unconfirmedRequests[state.selected.base];state.values[ODB.request]=state.values[ODB.handled];state.values[ODB.state]="Succeeded"');
  await finish(run('refresh()'));
  check('!element("apply-button").disabled && element("settings-lock").hidden',"late terminal acknowledgement permits re-Apply");
  run('state.unconfirmedRequests[state.selected.base]=state.values[ODB.handled]+1;updateUi()');
  run('state.mock=true;element("ip-address").value="192.168.10.41"');await finish(run('readSelectedIp()'));
  check('!element("apply-button").disabled',"uncertain device does not block another IP");

  reset();run('mjsonrpc_db_get_values=()=>new Promise(()=>{})');
  await finish(run('applySettings()'));
  check('!state.applyPending && !writtenPaths.length && element("settings-lock").hidden && element("apply-button").disabled',"hung preflight read sends no request and releases editing");

  reset();run('state.staged.asic1.code=615;mjsonrpc_db_get_values=()=>new Promise(()=>{})');
  await finish(run('saveSettings()'));
  check('state.loaded===null && state.staged.asic1.code===615 && !state.saving && !element("read-button").disabled && !element("ip-address").disabled && element("apply-button").disabled',"failed Save/reread preserves editable Desired and permits READ recovery");

  for(const mode of ["hungRead","waiting","disconnected"]) {
    reset();context.waitMode=mode;
    run(`var submitted=false;
      var normalRead=mjsonrpc_db_get_values;
      mjsonrpc_db_get_values=paths=>submitted&&waitMode==="hungRead"?new Promise(()=>{}):normalRead(paths);
      mjsonrpc_db_set_value=async(path,value)=>{
        writtenPaths.push(path);state.values[path]=value;submitted=true;
        state.values[ODB.active]=value;state.values[ODB.inProgress]=true;state.values[ODB.state]="Applying";
        if(waitMode==="disconnected")state.values[ODB.clients]={};
        return {result:{status:[1]}};
      };`);
    await finish(run('applySettings()'));
    check('!state.applyPending && element("settings-lock").hidden && element("apply-button").disabled && !element("read-button").disabled',`${mode}: editing/READ recover while re-Apply stays blocked`);
    check('state.staged.asic1.code===600 && element("message").className.includes("error")',`${mode}: Desired and error retained`);
  }
  run('state.mock=true;selectMockScenario("match");state.statusUnavailable=false;updateUi()');
  check('element("hv-voltage").textContent==="Unknown" && element("hv-current").textContent==="Unknown" && element("hv-enabled").textContent==="Unknown"', "missing HV schema is Unknown");
  run('state.values[ODB.hvValid]=true;state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000);state.values[ODB.hvVoltage]=12;state.values[ODB.hvCurrent]=21;state.values[ODB.hvLimit]=20;state.values[ODB.hvRamp]="Steady";state.values[ODB.hvTarget]=12;updateUi()');
  check('element("hv-current").className==="hv-over-limit" && element("hv-voltage").textContent.includes("12.000")', "fresh HV display and red over-current");
  check('element("hv-voltage-button").disabled && element("hv-enable-button").disabled', "unverified HV controls disabled");
  run('state.values[ODB.hvUpdated]-=66;updateUi()');
  check('element("hv-current").textContent==="Unknown" && element("hv-current").className===""', "stale values cannot imply safe current");
  run('state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000)-3;state.values[ODB.hvRamp]="Ramping";updateUi()');
  check('!hvMeasurementFresh()', "ramp stale cutoff");
  run('state.values[ODB.hvUpdated]=Date.now()/1000+100;updateUi()');
  check('!hvMeasurementFresh()', "future timestamp is Unknown");
  run('state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000);state.statusUnavailable=true;updateUi()');
  check('!hvMeasurementFresh() && element("hv-current").textContent==="Unknown"', "ODB error invalidates HV display");
  // Regression: legacy integer seconds and new precise sample timestamps.
  const investigationNow=now;
  now=1000900;
  run('state.statusUnavailable=false;state.values[ODB.hvValid]=true;state.values[ODB.hvUpdated]=1000;state.values[ODB.hvMonitoring]="OK";state.values[ODB.hvRamp]="Ramping";updateUi()');
  check('element("hv-voltage").textContent==="12.000 V"', "investigation: successful ramp sample displayed");
  now=1002100;run('updateUi()');
  check('statusValue("hvValid")===true && statusValue("hvMonitoring")==="OK" && element("hv-voltage").textContent==="12.000 V"',
    "legacy integer seconds do not expire early at true sample age 1.2 s");
  run('state.values[ODB.hvUpdatedMs]="0x00000000000f45c4";updateUi()'); // 1000900 ms
  check('hvMeasurementFresh() && element("hv-updated").textContent.includes(".900Z")',"precise sample time displayed with fraction");
  run('var preciseSelectedIp=element("ip-address").value;element("ip-address").value="192.168.10.99";updateUi()');
  check('element("hv-voltage").textContent==="Unknown" && element("hv-voltage-button").disabled',"precise timestamp cannot bypass IP selection guard");
  run('element("ip-address").value=preciseSelectedIp;state.statusUnavailable=true;updateUi()');
  check('element("hv-voltage").textContent==="Unknown"',"HTTP failure hides precise cached measurement immediately");
  run('state.statusUnavailable=false;state.values[ODB.hvUpdatedMs]=NaN;updateUi()');
  check('!hvMeasurementFresh()',"malformed precise time cannot fall back to old integer time");
  run('state.values[ODB.hvUpdatedMs]=Date.now()+1;updateUi()');
  check('!hvMeasurementFresh()',"future precise timestamp is invalid");
  run('state.values[ODB.hvUpdatedMs]=1000900;updateUi()');
  now=1003710;run('updateUi()');
  for (const ramp of ["Ramping","Settling","EmergencyDown"]) {
    context.investigationRamp=ramp;
    run('state.values[ODB.hvRamp]=investigationRamp;updateUi()');
    check('hvMeasurementFresh() && element("hv-voltage").textContent==="12.000 V"', "normal wait beyond 2 s remains displayed in all ramp states");
  }
  now=1006900;run('updateUi()');
  check('hvMeasurementFresh()',"ramp display valid at exact 6 s limit");
  now=1006901;run('updateUi()');
  check('!hvMeasurementFresh() && element("hv-current").textContent==="Unknown"',"stopped updates expire past 6 s even with cached Valid");
  run('state.values[ODB.hvRamp]="Steady";updateUi()');
  check('element("hv-voltage").textContent==="12.000 V"', "same ODB sample remains displayable in steady state");
  run('state.values[ODB.hvValid]=false;updateUi()');
  check('element("hv-voltage").textContent==="Unknown" && element("hv-current").textContent==="Unknown"', "invalid measurement hides retained numeric payload");
  run('state.values[ODB.hvValid]=true;state.reading=true;updateUi()');
  check('element("hv-voltage").textContent==="Unknown"', "READ hides cached measurements while pending");
  run('state.reading=false;updateUi()');
  const investigationTimestamp=run('statusValue("hvUpdated")');
  await run('readSelectedIp()');await run('refresh()');
  assert.equal(run('statusValue("hvUpdated")'),investigationTimestamp);++checks;
  check('element("hv-voltage").textContent==="12.000 V"', "READ and automatic poll reuse ODB measurement without sampling");
  now=1065900;run('updateUi()');
  check('hvMeasurementFresh()',"steady precise display remains valid at exact 65 s boundary");
  now=1065901;run('updateUi()');
  check('element("hv-voltage").textContent==="Unknown"', "steady display expires after 65 s despite successful READ/poll");
  now=investigationNow;
  run('delete state.values[ODB.hvUpdatedMs];state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000)');
  run(`state.statusUnavailable=false;state.values[ODB.runState]=1;state.values[ODB.transition]=0;
    state.values[ODB.inProgress]=false;state.values[ODB.clients]={mock:{name:"feeasiroc"}};
    ["hvEnableAvailable","hvVoltageAvailable","hvStopAvailable","hvEmergencyAvailable"].forEach(key=>state.values[ODB[key]]=true);
    ["hvRequest","hvHandled","hvActive","hvResultRequest"].forEach(key=>state.values[ODB[key]]=0);
    state.values[ODB.hvInProgress]=false;
    var hvDialogs=[];window.confirm=message=>{hvDialogs.push(message);return false;};element("hv-target").value="62";updateUi();`);
  const hvRequestBefore=run('statusValue("hvRequest")');
  const asicRequestBefore=run('statusValue("request")');
  await run('requestHv("Voltage")');
  check('hvDialogs.length===1 && hvDialogs[0].includes("62 V") && hvDialogs[0].includes(state.selected.ip)', "voltage confirmation includes value and IP");
  assert.equal(run('statusValue("hvRequest")'),hvRequestBefore);++checks;
  run('window.confirm=message=>{hvDialogs.push(message);return true;};element("hv-target").value="62.1"');
  await run('requestHv("Voltage")');
  check('hvDialogs.length===1 && element("hv-result").textContent.includes("0..62")', "reject above 62 before confirmation");
  run('element("hv-target").value="0"');await run('requestHv("Enable")');
  check('hvDialogs[1].includes("ON at 0 V") && statusValue("hvRequest")===1 && element("hv-result").textContent.includes("mock")', "mock Enable independent request");
  await run('requestHv("Stop")');
  check('hvDialogs[2].includes("Disable") && hvDialogs[2].includes("Shutdown") && hvDialogs[2].includes("verify ≤5 V") && hvDialogs[2].includes("10 V/step") && hvDialogs[2].includes("±5 V") && statusValue("hvRequest")===2', "normal stop confirmation");
  await run('requestHv("Emergency")');
  check('hvDialogs[3].includes("without waiting") && statusValue("hvRequest")===3', "emergency distinguished");
  run('element("hv-target").value="12.3";updateUi();updateUi()');
  check('element("hv-target").value==="12.3"', "refresh preserves staged target");
  for(const value of [2,3]) {
    run(`state.values[ODB.runState]=${value}`);await run('requestHv("Emergency")');
    check('element("hv-result").textContent.includes("STOPPED") && hvDialogs.length===4', "RUNNING/PAUSED forbid all HV");
  }
  run('state.values[ODB.runState]=1;state.values[ODB.transition]=1');await run('requestHv("Enable")');
  check('hvDialogs.length===4 && element("hv-result").textContent.includes("transition")', "DAQ transition forbids HV");
  run('state.values[ODB.transition]=0;state.values[DEVICE_ROOT].moduleA.IPAddress="192.168.10.99"');await run('requestHv("Enable")');
  check('hvDialogs.length===4 && element("hv-result").textContent.includes("registry")', "registry change forbids HV");
  assert.equal(run('statusValue("request")'),asicRequestBefore);++checks;

  // Real mailbox submission through RPC mocks, including lost ACK and recovery.
  run(`state.values[DEVICE_ROOT].moduleA.IPAddress="192.168.10.40";state.mock=false;writtenPaths=[];
    mjsonrpc_db_get_values=async paths=>({result:{status:paths.map(path=>present(state.values[path])?1:0),data:paths.map(path=>clone(state.values[path]??null))}});
    mjsonrpc_db_set_value=async(path,value)=>{writtenPaths.push(path);state.values[path]=value;return {result:{status:[1]}};};
    element("hv-target").value="62";`);
  await finish(run('requestHv("Voltage")'));
  check('writtenPaths.length===5 && writtenPaths.every(path=>path.includes("/Commands/HV/")) && writtenPaths[4]===ODB.hvRequest', "payload and RequestId commit independent of ASIC");
  check('state.values[`${state.selected.base}/Commands/HV/RequestPayload`]==="4 Voltage 62" && state.hvOutstanding[state.selected.base].id===4', "immutable request envelope");
  check('element("hv-voltage-button").disabled && !element("hv-emergency-button").disabled', "await acknowledgement while emergency stays available");
  run('state.values[ODB.hvHandled]=4;state.values[ODB.hvResultRequest]=4;state.values[ODB.hvResult]="Succeeded";updateUi()');
  check('!state.hvOutstanding[state.selected.base] && !element("hv-voltage-button").disabled', "frontend terminal acknowledgement clears pending");
  run('state.values[ODB.hvEnableAvailable]=false;element("hv-target").value="0";writtenPaths=[]');
  await finish(run('requestHv("Enable")'));
  check('!writtenPaths.length', "backend unsafe-Enable capability cannot be bypassed");
  run(`mjsonrpc_db_set_value=(path,value)=>{
    writtenPaths.push(path);state.values[path]=value;
    return path===ODB.hvRequest?new Promise(()=>{}):Promise.resolve({result:{status:[1]}});
  };`);
  await finish(run('requestHv("Emergency")'));
  check('state.hvOutstanding[state.selected.base].id===5 && !state.hvSubmitting && element("hv-result").textContent.includes("Unknown")', "lost submit ACK retains uncertainty");
  run(`mjsonrpc_db_set_value=async(path,value)=>{writtenPaths.push(path);state.values[path]=value;return {result:{status:[1]}};};`);
  await finish(run('requestHv("Emergency")'));
  check('state.hvOutstanding[state.selected.base].id===6 && statusValue("hvRequest")===6', "explicit emergency advances uncertain ID without replay");
  run('state.values[ODB.hvHandled]=6;state.values[ODB.hvResultRequest]=6;updateUi();state.values[ODB.inProgress]=true;writtenPaths=[]');
  await finish(run('requestHv("Emergency")'));
  check('!writtenPaths.length', "ASIC in progress forbids HV submission");
  run('state.values[ODB.inProgress]=false;element("ip-address").value="192.168.10.41";state.mock=true');await run('readSelectedIp()');
  check('state.selected.id==="moduleB" && !state.hvOutstanding[state.selected.base]', "other IP has independent pending state");
  run('selectMockScenario("hv_disabled")');
  check('statusValue("hvEnabled")==="OFF" && statusValue("hvVoltage")===0.8 && !element("hv-enable-button").disabled', "disabled mock preserves residual voltage and exposes simulated Enable");
  run('selectMockScenario("hv_settling");state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000);updateUi()');
  check('element("hv-set").textContent==="52.000 V" && element("hv-voltage").textContent==="60.000 V" && element("hv-result").textContent.includes("Pending") && !element("hv-emergency-button").disabled',
        "normal-down preview displays holding step and exposes Emergency");
  run('selectMockScenario("hv_overcurrent")');
  check('Number(statusValue("hvCurrent"))===21 && Number(statusValue("hvLimit"))===20', "overcurrent preview uses specified 20 uA limit");
  run('selectMockScenario("hv_error")');
  check('element("hv-voltage").textContent==="Unknown" && element("hv-result").textContent.includes("timeout")', "communication failure preview");
  run(`state.statusUnavailable=false;state.values[ODB.hvValid]=true;state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000);
    state.values[ODB.hvSetState]="Known";state.values[ODB.hvSet]=18;state.values[ODB.hvVoltage]=0.063;
    state.values[ODB.hvCurrent]=10;state.values[ODB.hvRamp]="Steady";state.values[ODB.hvResult]="Succeeded";
    state.values[ODB.hvError]="";state.values[ODB.hvMonitoring]="WARNING: measured voltage differs from setpoint by more than 1 V";updateUi();`);
  check('element("hv-set").textContent==="18.000 V" && element("hv-voltage").textContent==="0.063 V" && element("hv-current").textContent==="10.000 µA" && element("hv-ramp").textContent==="Steady" && element("hv-updated").textContent!=="Unknown"',
        "setpoint, actual measurement, current, status and timestamp displayed independently");
  check('element("hv-result").textContent.trim()==="Succeeded" && element("hv-monitoring").textContent.includes("WARNING")',"monitoring warning does not replace operation result");
  run('state.values[ODB.hvSetState]="Unknown";state.values[ODB.hvValid]=false;state.values[ODB.hvMonitoring]="HV hardware access revoked";updateUi()');
  check('element("hv-result").textContent.trim()==="Succeeded" && element("hv-monitoring").textContent.includes("revoked") && element("hv-set").textContent==="Unknown"',"permission revocation preserves operation history");
  assert.ok(!html.includes("昇圧4 V/step") && html.includes("RampStatus"));++checks;
  // Backend capability can admit initial Enable while every hardware fact is
  // Unknown. The browser submits a command; it never manufactures measurements.
  run(`state.mock=false;state.statusUnavailable=false;state.values[ODB.hvEnableAvailable]=true;
    state.values[ODB.hvError]="";state.values[ODB.hvBlocked]="";state.values[ODB.hvResult]="Idle";
    state.values[ODB.hvValid]=false;state.values[ODB.hvUpdated]=0;state.values[ODB.hvEnabled]="Unknown";
    writtenPaths=[];element("hv-target").value="0";updateUi();`);
  const initialAsicRequest=run('statusValue("request")');
  check('!element("hv-enable-button").disabled && element("hv-enabled").textContent==="Unknown" && element("hv-voltage").textContent==="Unknown"',
        "initial Enable capability does not assert known OFF or valid voltage");
  await finish(run('requestHv("Enable")'));
  check('writtenPaths.length===5 && writtenPaths.every(path=>path.includes("/Commands/HV/")) && state.values[`${state.selected.base}/Commands/HV/RequestPayload`]==="1 Enable 0"',
        "initial Unknown Enable submits only a zero-target HV envelope");
  check('statusValue("hvEnabled")==="Unknown" && statusValue("hvValid")===false && statusValue("hvUpdated")===0 && state.hvOutstanding[state.selected.base].id===1',
        "submission leaves Unknown and unmeasured status unchanged while waiting");
  assert.equal(run('statusValue("request")'),initialAsicRequest);++checks;
  // A normal Stop acknowledged by the frontend may wait indefinitely while
  // its measurements remain fresh. Local ACK timeout must not mask Pending.
  run(`state.values[ODB.hvRequest]=2;state.values[ODB.hvHandled]=1;
    state.values[ODB.hvActive]=2;state.values[ODB.hvResultRequest]=2;state.values[ODB.hvResult]="Pending";
    state.values[ODB.hvError]="";state.values[ODB.hvMonitoring]="OK";state.values[ODB.hvInProgress]=true;
    state.values[ODB.hvValid]=true;state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000);
    state.values[ODB.hvSetState]="Known";state.values[ODB.hvSet]=52;state.values[ODB.hvVoltage]=60;
    state.values[ODB.hvCurrent]=1;state.values[ODB.hvRamp]="Settling";state.values[ODB.hvEmergencyAvailable]=true;
    state.hvOutstanding[state.selected.base]={id:2,started:Date.now()-300000};writtenPaths=[];updateUi();`);
  check('element("hv-result").textContent.includes("Pending") && !element("hv-result").textContent.includes("timed out") && !element("hv-emergency-button").disabled && element("hv-voltage-button").disabled',
        "long acknowledged Stop stays Pending with Emergency available");
  check('element("hv-set").textContent==="52.000 V" && element("hv-voltage").textContent==="60.000 V" && element("hv-ramp").textContent==="Settling" && element("hv-monitoring").textContent==="OK"',
        "untracked descent continues displaying setting, measurements and state without extra warning");
  run('state.values[ODB.hvUpdated]-=3;updateUi()');
  check('element("hv-result").textContent.includes("timed out") && !element("hv-emergency-button").disabled',"stale measurements cannot manufacture healthy indefinite Pending");
  run('state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000);updateUi()');
  await finish(run('requestHv("Emergency")'));
  check('statusValue("hvRequest")===3 && state.values[`${state.selected.base}/Commands/HV/RequestPayload`]==="3 Emergency 0" && writtenPaths.length===5 && writtenPaths.every(path=>path.includes("/Commands/HV/"))',
        "long Stop can be superseded by Emergency through actual request path");
  run('state.values[ODB.hvActive]=0;state.values[ODB.hvHandled]=3;state.values[ODB.hvResultRequest]=3;state.values[ODB.hvResult]="Succeeded";state.values[ODB.hvInProgress]=false;updateUi();updateUi()');
  check('!state.hvOutstanding[state.selected.base] && writtenPaths.length===5 && statusValue("request")==='+initialAsicRequest,
        "Emergency terminal clears old Stop wait without resubmission or ASIC request change");
  assert.ok(html.includes('disabled>Enable</button>') && html.includes('disabled>Disable</button>'));++checks;
  // IP editing is local; HV identity is established only by a successful READ.
  const candidates=html.match(/<datalist id="ip-candidates">([\s\S]*?)<\/datalist>/)[1];
  assert.deepEqual([...candidates.matchAll(/value="([^"]+)"/g)].map(m=>m[1]),
    ["192.168.10.40","192.168.10.41","192.168.10.42","192.168.10.43"]);++checks;
  assert.ok(html.includes('list="ip-candidates"') && html.includes('id="ip-address" type="text"'));++checks;
  run('selectMockScenario("hv_disabled");state.hvOutstanding={};state.mock=false;var selectionReads=0;writtenPaths=[];mjsonrpc_db_get_values=async paths=>{selectionReads++;return {result:{status:paths.map(path=>present(state.values[path])?1:0),data:paths.map(path=>clone(state.values[path]??null))}}};');
  check('hvSelectionMatches() && !element("hv-enable-button").disabled',"baseline selected HV is available");
  const unchanged=run('JSON.stringify(state.values)');
  run('element("ip-address").value="192.168.10.41";element("ip-address").listeners.input()');
  check('selectionReads===0 && writtenPaths.length===0',"IP input causes no ODB read/write or hardware request");
  assert.equal(run('JSON.stringify(state.values)'),unchanged);++checks;
  check('["hv-enabled","hv-set","hv-voltage","hv-current","hv-monitoring","hv-ramp","hv-limit","hv-updated"].every(id=>element(id).textContent==="Unknown") && element("hv-device").textContent===""',"different input IP cannot inherit any old HV state");
  check('["hv-enable-button","hv-voltage-button","hv-stop-button","hv-emergency-button","hv-target"].every(id=>element(id).disabled)',"all HV actions blocked until matching READ");
  run('state.reading=true;updateHvUi()');
  check('element("hv-voltage").textContent==="Unknown" && element("hv-emergency-button").disabled',"in-flight READ cannot display prior device");
  run('state.reading=false');
  await run('readSelectedIp()');
  check('state.selected.ip==="192.168.10.41" && element("hv-device").textContent==="192.168.10.41"',"explicit READ selects input IP through registry/backend");
  run('element("ip-address").value="10.20.30.40";element("ip-address").listeners.change()');
  check('canonicalIp(element("ip-address").value) && element("hv-enabled").textContent==="Unknown"',"free input accepts IPv4 outside suggestions");
  await run('readSelectedIp()');
  check('element("message").textContent.includes("Unregistered IP") && element("hv-enabled").textContent==="Unknown" && element("hv-emergency-button").disabled',"unregistered READ preserves registry safety and hides previous HV");
  for(const invalid of ["", "192.168.10.256", "192.168.10", "192.168.10.040", "example.com", "192.168.10.40/24"]) {
    run(`element("ip-address").value=${JSON.stringify(invalid)};selectionReads=0;writtenPaths=[];element("ip-address").listeners.input()`);
    await run('readSelectedIp()');
    check('selectionReads===0 && writtenPaths.length===0 && element("message").textContent.includes("IPv4")',`invalid IPv4 rejected before backend: ${invalid}`);
  }
  run('selectMockScenario("hv_disabled");state.hvOutstanding={};state.values[ODB.hvResult]="Idle";state.values[ODB.hvUpdated]=Math.floor(Date.now()/1000);updateUi()');
  check('element("hv-result").hidden && element("hv-validity").hidden',"idle/valid status has no explanatory noise");
  run('state.values[ODB.hvValid]=false;state.values[ODB.hvError]="RBCP reply timeout";state.values[ODB.hvResult]="Indeterminate";updateUi()');
  check('!element("hv-validity").hidden && !element("hv-result").hidden && element("hv-result").textContent.includes("timeout")',"invalid measurement and communication error remain visible");
  console.log(`easiroc_web_mock_test: ${checks} checks passed`);
})().catch(error=>{console.error(error);process.exitCode=1;});
