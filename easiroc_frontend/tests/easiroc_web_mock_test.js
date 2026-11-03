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
  run('state.staged.asic1.input[0]=350;state.values[ODB.runState]=3;updateUi()');
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
  console.log(`easiroc_web_mock_test: ${checks} checks passed`);
})().catch(error=>{console.error(error);process.exitCode=1;});
