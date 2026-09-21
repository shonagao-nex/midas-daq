"use strict";

const MIDAS_SUCCESS = 1;
const REFRESH_INTERVAL_MS = 1000;
const APPLY_TIMEOUT_MS = 45000;
const ODB = Object.freeze({
  runState: "/Runinfo/State",
  enabled: "/Equipment/EASIROC/Settings/Enabled",
  applyAtBor: "/Equipment/EASIROC/Settings/ASICSlowControl/ApplyAtBOR",
  asic1Code: "/Equipment/EASIROC/Settings/ASIC1/DiscriminatorDACCode",
  asic1Slope: "/Equipment/EASIROC/Settings/ASIC1/DiscriminatorDACSlope",
  asic1HgFeedback: "/Equipment/EASIROC/Settings/ASIC1/HGFeedbackCapacitance",
  asic1LgFeedback: "/Equipment/EASIROC/Settings/ASIC1/LGFeedbackCapacitance",
  asic1HgShaping: "/Equipment/EASIROC/Settings/ASIC1/HGShapingTime",
  asic1LgShaping: "/Equipment/EASIROC/Settings/ASIC1/LGShapingTime",
  asic1Input: "/Equipment/EASIROC/Settings/ASIC1/InputDAC",
  asic1ChannelEnabled: "/Equipment/EASIROC/Settings/ASIC1/ChannelEnabled",
  asic2Code: "/Equipment/EASIROC/Settings/ASIC2/DiscriminatorDACCode",
  asic2Slope: "/Equipment/EASIROC/Settings/ASIC2/DiscriminatorDACSlope",
  asic2HgFeedback: "/Equipment/EASIROC/Settings/ASIC2/HGFeedbackCapacitance",
  asic2LgFeedback: "/Equipment/EASIROC/Settings/ASIC2/LGFeedbackCapacitance",
  asic2HgShaping: "/Equipment/EASIROC/Settings/ASIC2/HGShapingTime",
  asic2LgShaping: "/Equipment/EASIROC/Settings/ASIC2/LGShapingTime",
  asic2Input: "/Equipment/EASIROC/Settings/ASIC2/InputDAC",
  asic2ChannelEnabled: "/Equipment/EASIROC/Settings/ASIC2/ChannelEnabled",
  request: "/Equipment/EASIROC/Commands/ASICSlowControl/ApplyRequestId",
  active: "/Equipment/EASIROC/Variables/ASICSlowControl/ActiveRequestId",
  handled: "/Equipment/EASIROC/Variables/ASICSlowControl/LastHandledRequestId",
  successful: "/Equipment/EASIROC/Variables/ASICSlowControl/LastSuccessfulRequestId",
  state: "/Equipment/EASIROC/Variables/ASICSlowControl/ApplyState",
  inProgress: "/Equipment/EASIROC/Variables/ASICSlowControl/ApplyInProgress",
  attemptSucceeded: "/Equipment/EASIROC/Variables/ASICSlowControl/LastAttemptSucceeded",
  applyError: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplyError",
  applyTime: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplyUnixTime",
  configurationMatch: "/Equipment/EASIROC/Variables/ASICSlowControl/ConfigurationMatch",
  configurationStatus: "/Equipment/EASIROC/Variables/ASICSlowControl/ConfigurationStatus",
  configurationDetail: "/Equipment/EASIROC/Variables/ASICSlowControl/ConfigurationDetail",
  indeterminate: "/Equipment/EASIROC/Variables/ASICSlowControl/HardwareStateIndeterminate",
  lastValid: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/Valid",
  lastRequest: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/RequestId",
  lastTime: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ApplyUnixTime",
  lastAsic1Code: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/DiscriminatorDACCode",
  lastAsic1Slope: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/DiscriminatorDACSlope",
  lastAsic1HgFeedback: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/HGFeedbackCapacitance",
  lastAsic1LgFeedback: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/LGFeedbackCapacitance",
  lastAsic1HgShaping: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/HGShapingTime",
  lastAsic1LgShaping: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/LGShapingTime",
  lastAsic1Input: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/InputDAC",
  lastAsic1ChannelEnabled: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC1/ChannelEnabled",
  lastAsic2Code: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/DiscriminatorDACCode",
  lastAsic2Slope: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/DiscriminatorDACSlope",
  lastAsic2HgFeedback: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/HGFeedbackCapacitance",
  lastAsic2LgFeedback: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/LGFeedbackCapacitance",
  lastAsic2HgShaping: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/HGShapingTime",
  lastAsic2LgShaping: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/LGShapingTime",
  lastAsic2Input: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/InputDAC",
  lastAsic2ChannelEnabled: "/Equipment/EASIROC/Variables/ASICSlowControl/LastApplied/ASIC2/ChannelEnabled"
});
const PATHS = Object.values(ODB);
const SETTINGS_PATHS = [ODB.asic1Code, ODB.asic1Slope, ODB.asic1HgFeedback, ODB.asic1LgFeedback, ODB.asic1HgShaping, ODB.asic1LgShaping, ODB.asic1Input,
  ODB.asic1ChannelEnabled, ODB.asic2Code, ODB.asic2Slope, ODB.asic2HgFeedback, ODB.asic2LgFeedback, ODB.asic2HgShaping, ODB.asic2LgShaping, ODB.asic2Input, ODB.asic2ChannelEnabled];
const FEEDBACK_OPTIONS = Object.freeze([[0, 0], [100, 8], [200, 4], [300, 12], [400, 2], [500, 10], [600, 6], [700, 14], [800, 1], [900, 9], [1000, 5], [1100, 13], [1200, 3], [1300, 11], [1400, 7], [1500, 15]]);
const SHAPING_OPTIONS = Object.freeze([[25, 1], [50, 2], [75, 3], [100, 4], [125, 5], [150, 6], [175, 7]]);
const TERMINAL_STATES = new Set(["Succeeded", "Failed", "Rejected", "Indeterminate"]);
const state = {values: {}, loaded: null, staged: null, mock: false, scenario: "match", saving: false, applyPending: false, requestedId: null};

const element = id => document.getElementById(id);
const present = value => value !== null && value !== undefined;
const truth = value => value === true || Number(value) === 1;
const boolText = value => present(value) ? truth(value) ? "TRUE" : "FALSE" : "—";
const integer = value => Number.isInteger(Number(value)) ? Number(value) : NaN;
const runState = value => Number(value) === 1 ? "STOPPED" : Number(value) === 2 ? "PAUSED" : Number(value) === 3 ? "RUNNING" : "—";
const statusValue = key => state.values[ODB[key]];
const setText = (id, value, fallback = "—") => { element(id).textContent = present(value) && value !== "" ? String(value) : fallback; };
const clone = value => JSON.parse(JSON.stringify(value));

function settingsFromValues(values) {
  const input = path => Array.isArray(values[path]) && values[path].length === 32 ? values[path].map(Number) : null;
  const enabled = path => Array.isArray(values[path]) && values[path].length === 32 ? values[path].map(truth) : null;
  const asic = (code, slope, hgFeedback, lgFeedback, hgShaping, lgShaping, dac, channelEnabled) => ({code: integer(values[code]), slope: integer(values[slope]), hgFeedback: integer(values[hgFeedback]), lgFeedback: integer(values[lgFeedback]), hgShaping: integer(values[hgShaping]), lgShaping: integer(values[lgShaping]), input: input(dac), channelEnabled: enabled(channelEnabled)});
  return {asic1: asic(ODB.asic1Code, ODB.asic1Slope, ODB.asic1HgFeedback, ODB.asic1LgFeedback, ODB.asic1HgShaping, ODB.asic1LgShaping, ODB.asic1Input, ODB.asic1ChannelEnabled), asic2: asic(ODB.asic2Code, ODB.asic2Slope, ODB.asic2HgFeedback, ODB.asic2LgFeedback, ODB.asic2HgShaping, ODB.asic2LgShaping, ODB.asic2Input, ODB.asic2ChannelEnabled)};
}

function settingsAvailable(settings) {
  return [settings.asic1, settings.asic2].every(asic => Number.isInteger(asic.code) && Number.isInteger(asic.slope) && Number.isInteger(asic.hgFeedback) && Number.isInteger(asic.lgFeedback) && Number.isInteger(asic.hgShaping) && Number.isInteger(asic.lgShaping) && Array.isArray(asic.input) && asic.input.length === 32 && Array.isArray(asic.channelEnabled) && asic.channelEnabled.length === 32);
}

function sameSettings(left, right) {
  if (!left || !right) return false;
  return ["asic1", "asic2"].every(name => left[name].code === right[name].code && left[name].slope === right[name].slope && left[name].hgFeedback === right[name].hgFeedback && left[name].lgFeedback === right[name].lgFeedback && left[name].hgShaping === right[name].hgShaping && left[name].lgShaping === right[name].lgShaping && left[name].input.length === right[name].input.length && left[name].input.every((value, index) => value === right[name].input[index]) && left[name].channelEnabled.length === right[name].channelEnabled.length && left[name].channelEnabled.every((value, index) => value === right[name].channelEnabled[index]));
}

function validateSettings(settings) {
  for (const [name, asic] of [["ASIC1", settings.asic1], ["ASIC2", settings.asic2]]) {
    if (!Number.isInteger(asic.code) || asic.code < 0 || asic.code > 1023) return `${name} Discriminator DAC Code must be 0..1023`;
    if (asic.slope !== 0 && asic.slope !== 1) return `${name} Discriminator Slope must be 0 or 1`;
    if (!FEEDBACK_OPTIONS.some(([femtofarads]) => femtofarads === asic.hgFeedback)) return `${name} HG Feedback Capacitance must be a supported option`;
    if (!FEEDBACK_OPTIONS.some(([femtofarads]) => femtofarads === asic.lgFeedback)) return `${name} LG Feedback Capacitance must be a supported option`;
    if (!SHAPING_OPTIONS.some(([nanoseconds]) => nanoseconds === asic.hgShaping)) return `${name} HG Shaping Time must be a supported option`;
    if (!SHAPING_OPTIONS.some(([nanoseconds]) => nanoseconds === asic.lgShaping)) return `${name} LG Shaping Time must be a supported option`;
    for (let channel = 0; channel < 32; ++channel) if (!Number.isInteger(asic.input[channel]) || asic.input[channel] < 0 || asic.input[channel] > 511) return `${name} InputDAC ch ${channel} must be 0..511`;
  }
  return "";
}

function showMessage(kind, message) {
  const node = element("message");
  node.className = `message ${kind}`;
  node.textContent = message;
  node.hidden = !message;
}

function clearMessage() { element("message").hidden = true; }

function unixTime(value) {
  const seconds = Number(value);
  return Number.isFinite(seconds) && seconds > 0 ? new Date(seconds * 1000).toLocaleString() : "—";
}

function renderInputDac(asic) {
  const container = element(`${asic}-input-dac`);
  container.replaceChildren();
  const staged = state.staged[asic];
  staged.input.forEach((value, channel) => {
    const cell = document.createElement("div"); cell.className = "dac-cell";
    const label = document.createElement("label"); label.htmlFor = `${asic}-input-${channel}`; label.textContent = `ch ${channel}`;
    const input = document.createElement("input"); input.id = `${asic}-input-${channel}`; input.type = "number"; input.min = "0"; input.max = "511"; input.step = "1"; input.value = String(value);
    input.addEventListener("input", () => { state.staged[asic].input[channel] = integer(input.value); updateUi(); });
    cell.append(label, input); container.appendChild(cell);
  });
}

function buildInputGrid() { renderInputDac("asic1"); renderInputDac("asic2"); }

function renderChannelMask(asic) {
  const container = element(`${asic}-channel-mask`);
  container.replaceChildren();
  state.staged[asic].channelEnabled.forEach((enabled, channel) => {
    const cell = document.createElement("label"); cell.className = "mask-cell";
    const checkbox = document.createElement("input"); checkbox.type = "checkbox"; checkbox.checked = enabled;
    checkbox.addEventListener("change", () => { state.staged[asic].channelEnabled[channel] = checkbox.checked; updateUi(); });
    const text = document.createElement("span"); text.textContent = `ch ${channel} enabled`;
    cell.append(checkbox, text); container.appendChild(cell);
  });
}

function buildChannelMaskGrids() { renderChannelMask("asic1"); renderChannelMask("asic2"); }

function populateFeedbackSelect(select) {
  if (select.options.length) return;
  FEEDBACK_OPTIONS.forEach(([femtofarads, code]) => {
    const option = document.createElement("option"); option.value = String(femtofarads);
    option.textContent = femtofarads === 0 ? "0 fF — NoC (code 0)" : `${femtofarads} fF (code ${code})`;
    select.appendChild(option);
  });
}

function populateShapingSelect(select) {
  if (select.options.length) return;
  SHAPING_OPTIONS.forEach(([nanoseconds, code]) => {
    const option = document.createElement("option"); option.value = String(nanoseconds);
    option.textContent = `${nanoseconds} ns (code ${code})`;
    select.appendChild(option);
  });
}

function syncSettingInputs() {
  ["asic1", "asic2"].forEach(asic => {
    element(`${asic}-dac-code`).value = String(state.staged[asic].code);
    element(`${asic}-dac-slope`).value = String(state.staged[asic].slope);
    ["hg", "lg"].forEach(gain => { const select = element(`${asic}-${gain}-feedback`); populateFeedbackSelect(select); select.value = String(state.staged[asic][`${gain}Feedback`]); });
    ["hg", "lg"].forEach(gain => { const select = element(`${asic}-${gain}-shaping`); populateShapingSelect(select); select.value = String(state.staged[asic][`${gain}Shaping`]); });
  });
  buildInputGrid();
  buildChannelMaskGrids();
}

function renderLastApplied() {
  setText("last-applied-valid", boolText(statusValue("lastValid")));
  setText("last-applied-request-id", statusValue("lastRequest"));
  setText("last-applied-unix-time", unixTime(statusValue("lastTime")));
  const details = ["asic1", "asic2"].map(asic => {
    const upper = asic.toUpperCase();
    const prefix = `last${upper.charAt(0) + upper.slice(1).toLowerCase()}`;
    const code = statusValue(`${prefix}Code`), slope = statusValue(`${prefix}Slope`), hg = statusValue(`${prefix}HgFeedback`), lg = statusValue(`${prefix}LgFeedback`), hgShaping = statusValue(`${prefix}HgShaping`), lgShaping = statusValue(`${prefix}LgShaping`), input = statusValue(`${prefix}Input`), channelEnabled = statusValue(`${prefix}ChannelEnabled`);
    const text = Array.isArray(input) ? input.map((value, index) => `ch${index}=${value}`).join("  ") : "not available";
    const pre = document.createElement("pre");
    const maskText = Array.isArray(channelEnabled) ? channelEnabled.map((value, index) => truth(value) ? null : `ch${index}`).filter(Boolean).join(", ") || "none" : "not available";
    pre.textContent = `${upper}\nDAC Code: ${present(code) ? code : "—"}\nSlope: ${present(slope) ? slope : "—"}\nHG Feedback: ${present(hg) ? `${hg} fF` : "—"}\nLG Feedback: ${present(lg) ? `${lg} fF` : "—"}\nHG Shaping: ${present(hgShaping) ? `${hgShaping} ns` : "—"}\nLG Shaping: ${present(lgShaping) ? `${lgShaping} ns` : "—"}\nMasked channels: ${maskText}\nInputDAC: ${text}`;
    return pre;
  });
  element("last-applied-details").replaceChildren(...details);
}

function editAllowed() { return runState(statusValue("runState")) === "STOPPED"; }
function statusAvailable() { return ["state", "inProgress", "handled", "successful", "active", "request"].every(key => present(statusValue(key))); }
function unsaved() { return !sameSettings(state.staged, state.loaded); }

function updateUi() {
  if (!state.staged || !state.loaded) return;
  const run = runState(statusValue("runState"));
  const isUnsaved = unsaved();
  const locked = !editAllowed();
  const validation = validateSettings(state.staged);
  const applying = truth(statusValue("inProgress")) || state.applyPending;
  const enabled = truth(statusValue("enabled"));
  setText("run-state", run);
  element("run-state").className = run === "STOPPED" ? "stopped" : "active";
  element("settings-lock").hidden = !locked;
  element("unsaved-state").textContent = isUnsaved ? "Unsaved changes" : "Saved";
  element("unsaved-state").className = `unsaved ${isUnsaved ? "dirty" : "clean"}`;
  document.querySelectorAll(".settings-panel input,.settings-panel select").forEach(input => { input.disabled = locked || state.saving; });
  element("save-button").disabled = locked || state.saving || !settingsAvailable(state.staged) || Boolean(validation);
  element("apply-button").disabled = locked || !enabled || applying || isUnsaved || !statusAvailable() || state.saving;
  setText("save-state", validation || (locked ? "Editing locked" : isUnsaved ? "Unsaved changes" : "Saved"));
  setText("operation-note", `Enabled: ${enabled ? "TRUE" : "FALSE"}; Apply requires STOPPED, saved valid Settings, and an idle frontend.`);
  setText("apply-state", statusValue("state")); setText("apply-in-progress", boolText(statusValue("inProgress")));
  setText("active-request-id", statusValue("active")); setText("last-handled-request-id", statusValue("handled")); setText("last-successful-request-id", statusValue("successful"));
  setText("last-attempt-succeeded", boolText(statusValue("attemptSucceeded"))); setText("last-apply-error", statusValue("applyError"), ""); setText("last-apply-unix-time", unixTime(statusValue("applyTime")));
  const configuration = String(statusValue("configurationStatus") || "Unknown");
  setText("configuration-status", configuration.toUpperCase()); element("configuration-status").className = `configuration-status ${configuration.toLowerCase()}`;
  setText("configuration-match", boolText(statusValue("configurationMatch"))); setText("configuration-detail", statusValue("configurationDetail"), ""); setText("hardware-state-indeterminate", boolText(statusValue("indeterminate")));
  setText("apply-at-bor", truth(statusValue("applyAtBor")) ? "TRUE — must remain FALSE" : "FALSE — required");
  renderLastApplied();
}

function valuesFromRpc(rpc) {
  const values = {};
  PATHS.forEach((path, index) => { values[path] = rpc.result.status[index] === MIDAS_SUCCESS ? rpc.result.data[index] : null; });
  return values;
}

async function readValues(paths = PATHS) {
  if (state.mock) return Object.fromEntries(paths.map(path => [path, state.values[path]]));
  const rpc = await mjsonrpc_db_get_values(paths);
  const values = {};
  paths.forEach((path, index) => { values[path] = rpc.result.status[index] === MIDAS_SUCCESS ? rpc.result.data[index] : null; });
  return values;
}

async function refresh() {
  try {
    const values = await readValues(); state.values = values;
    const loaded = settingsFromValues(values);
    if (settingsAvailable(loaded)) {
      state.loaded = loaded;
      if (!state.staged || !unsaved()) { state.staged = clone(loaded); syncSettingInputs(); }
    }
    updateUi();
  } catch (error) { showMessage("error", `Cannot refresh MIDAS status: ${decodeError(error)}`); }
  finally { window.setTimeout(refresh, REFRESH_INTERVAL_MS); }
}

function requireStopped() { if (!editAllowed()) throw new Error("Settings and apply are allowed only while Run state is STOPPED"); }

async function saveSettings() {
  if (state.saving || !state.staged) return;
  try {
    clearMessage();
    requireStopped();
    const validation = validateSettings(state.staged); if (validation) throw new Error(validation);
    state.saving = true; updateUi();
    const values = [state.staged.asic1.code, state.staged.asic1.slope, state.staged.asic1.hgFeedback, state.staged.asic1.lgFeedback, state.staged.asic1.hgShaping, state.staged.asic1.lgShaping, state.staged.asic1.input, state.staged.asic1.channelEnabled, state.staged.asic2.code, state.staged.asic2.slope, state.staged.asic2.hgFeedback, state.staged.asic2.lgFeedback, state.staged.asic2.hgShaping, state.staged.asic2.lgShaping, state.staged.asic2.input, state.staged.asic2.channelEnabled];
    if (state.mock) SETTINGS_PATHS.forEach((path, index) => { state.values[path] = clone(values[index]); });
    else {
      for (let index = 0; index < SETTINGS_PATHS.length; ++index) {
        const rpc = await mjsonrpc_db_set_value(SETTINGS_PATHS[index], values[index]);
        if (rpc.result.status[0] !== MIDAS_SUCCESS) throw new Error(`ODB write failed for ${SETTINGS_PATHS[index]}`);
      }
    }
    const readback = await readValues(SETTINGS_PATHS);
    const check = settingsFromValues(readback);
    if (!sameSettings(state.staged, check)) throw new Error("ODB readback did not match all saved Settings");
    Object.assign(state.values, readback); state.loaded = clone(check); state.staged = clone(check); syncSettingInputs();
    showMessage("success", "Saved: ODB readback matches all ASIC Settings.");
  } catch (error) { showMessage("error", `Save failed: ${decodeError(error)}`); }
  finally { state.saving = false; updateUi(); }
}

function uint32(value) { const number = Number(value); return Number.isFinite(number) && number >= 0 && number <= 0xffffffff ? Math.floor(number) : 0; }
function nextRequestId() {
  const highest = Math.max(uint32(statusValue("request")), uint32(statusValue("active")), uint32(statusValue("handled")), uint32(statusValue("successful")));
  if (highest >= 0xffffffff) throw new Error("ApplyRequestId exhausted: backend uses ordered DWORD IDs, so wraparound is unsafe");
  return highest + 1;
}

async function waitForTerminal(requestId) {
  const deadline = Date.now() + APPLY_TIMEOUT_MS;
  while (Date.now() < deadline) {
    const values = await readValues(); Object.assign(state.values, values); updateUi();
    const handled = uint32(statusValue("handled")), currentState = String(statusValue("state") || "");
    if (handled >= requestId && TERMINAL_STATES.has(currentState) && uint32(statusValue("active")) === 0) return currentState;
    await new Promise(resolve => window.setTimeout(resolve, 500));
  }
  throw new Error(`Timed out waiting for frontend acknowledgement of ApplyRequestId ${requestId}`);
}

async function applySettings() {
  if (state.applyPending) return;
  try {
    clearMessage();
    requireStopped();
    if (!truth(statusValue("enabled"))) throw new Error("EASIROC Enabled must be TRUE");
    if (unsaved()) throw new Error("Save to ODB before Apply to hardware");
    if (truth(statusValue("inProgress")) || !statusAvailable()) throw new Error("Frontend apply status is not ready");
    const requestId = nextRequestId(); state.applyPending = true; state.requestedId = requestId; updateUi();
    if (state.mock) {
      state.values[ODB.request] = requestId; state.values[ODB.active] = requestId; state.values[ODB.state] = "Applying"; state.values[ODB.inProgress] = true; updateUi();
      await new Promise(resolve => window.setTimeout(resolve, 350));
      state.values[ODB.active] = 0; state.values[ODB.handled] = requestId; state.values[ODB.successful] = requestId; state.values[ODB.state] = "Succeeded"; state.values[ODB.inProgress] = false; state.values[ODB.attemptSucceeded] = true; state.values[ODB.applyError] = ""; state.values[ODB.configurationStatus] = "Match"; state.values[ODB.configurationMatch] = true;
      state.values[ODB.lastValid] = true; state.values[ODB.lastRequest] = requestId; state.values[ODB.lastTime] = Math.floor(Date.now() / 1000);
      state.values[ODB.lastAsic1Code] = state.loaded.asic1.code; state.values[ODB.lastAsic1Slope] = state.loaded.asic1.slope; state.values[ODB.lastAsic1Input] = clone(state.loaded.asic1.input);
      state.values[ODB.lastAsic1ChannelEnabled] = clone(state.loaded.asic1.channelEnabled);
      state.values[ODB.lastAsic1HgFeedback] = state.loaded.asic1.hgFeedback; state.values[ODB.lastAsic1LgFeedback] = state.loaded.asic1.lgFeedback; state.values[ODB.lastAsic1HgShaping] = state.loaded.asic1.hgShaping; state.values[ODB.lastAsic1LgShaping] = state.loaded.asic1.lgShaping;
      state.values[ODB.lastAsic2Code] = state.loaded.asic2.code; state.values[ODB.lastAsic2Slope] = state.loaded.asic2.slope; state.values[ODB.lastAsic2Input] = clone(state.loaded.asic2.input);
      state.values[ODB.lastAsic2ChannelEnabled] = clone(state.loaded.asic2.channelEnabled);
      state.values[ODB.lastAsic2HgFeedback] = state.loaded.asic2.hgFeedback; state.values[ODB.lastAsic2LgFeedback] = state.loaded.asic2.lgFeedback; state.values[ODB.lastAsic2HgShaping] = state.loaded.asic2.hgShaping; state.values[ODB.lastAsic2LgShaping] = state.loaded.asic2.lgShaping;
      showMessage("success", `Frontend acknowledged ApplyRequestId ${requestId}: Succeeded.`);
    } else {
      const rpc = await mjsonrpc_db_set_value(ODB.request, requestId);
      if (rpc.result.status[0] !== MIDAS_SUCCESS) throw new Error("ApplyRequestId ODB write failed");
      const terminal = await waitForTerminal(requestId);
      showMessage(terminal === "Succeeded" ? "success" : "warning", `Frontend acknowledged ApplyRequestId ${requestId}: ${terminal}.`);
    }
  } catch (error) { showMessage("error", `Apply request failed: ${decodeError(error)}`); }
  finally { state.applyPending = false; state.requestedId = null; updateUi(); }
}

function decodeError(error) { return typeof mjsonrpc_decode_error === "function" ? mjsonrpc_decode_error(error) : String(error); }

function selectMockScenario(name) {
  const source = window.EASIROC_MOCK_SCENARIOS && window.EASIROC_MOCK_SCENARIOS[name]; if (!source) return;
  state.scenario = name; state.values = clone(source); state.loaded = settingsFromValues(state.values); state.staged = clone(state.loaded); syncSettingInputs();
  if (name === "unsaved") state.staged.asic1.input[0] += 1;
  document.querySelectorAll("[data-mock-scenario]").forEach(button => button.classList.toggle("active", button.dataset.mockScenario === name)); updateUi();
}

function setupEvents() {
  ["asic1", "asic2"].forEach(asic => {
    element(`${asic}-dac-code`).addEventListener("input", input => { if (!state.staged) return; state.staged[asic].code = integer(input.target.value); updateUi(); });
    element(`${asic}-dac-slope`).addEventListener("change", input => { if (!state.staged) return; state.staged[asic].slope = integer(input.target.value); updateUi(); });
    element(`${asic}-hg-feedback`).addEventListener("change", input => { if (!state.staged) return; state.staged[asic].hgFeedback = integer(input.target.value); updateUi(); });
    element(`${asic}-lg-feedback`).addEventListener("change", input => { if (!state.staged) return; state.staged[asic].lgFeedback = integer(input.target.value); updateUi(); });
    element(`${asic}-hg-shaping`).addEventListener("change", input => { if (!state.staged) return; state.staged[asic].hgShaping = integer(input.target.value); updateUi(); });
    element(`${asic}-lg-shaping`).addEventListener("change", input => { if (!state.staged) return; state.staged[asic].lgShaping = integer(input.target.value); updateUi(); });
  });
  element("save-button").addEventListener("click", saveSettings); element("apply-button").addEventListener("click", applySettings);
}

function initializeEasirocPage() {
  const query = new URLSearchParams(window.location.search); state.mock = query.has("mock");
  setupEvents();
  if (state.mock) {
    document.body.classList.add("mock-mode");
    element("mock-panel").hidden = false;
    const names = ["match", "mismatch", "unknown", "indeterminate", "applying", "succeeded", "failed", "rejected", "unsaved", "nondefault_shaping", "masked_channel"];
    names.forEach(name => { const button = document.createElement("button"); button.type = "button"; button.dataset.mockScenario = name; button.textContent = name.toUpperCase(); button.addEventListener("click", () => selectMockScenario(name)); element("mock-buttons").appendChild(button); });
    const requestedScenario = query.get("mock");
    selectMockScenario(window.EASIROC_MOCK_SCENARIOS[requestedScenario] ? requestedScenario : "match");
  } else {
    mhttpd_init("EASIROC Slow Control", REFRESH_INTERVAL_MS);
  }
  refresh();
}
