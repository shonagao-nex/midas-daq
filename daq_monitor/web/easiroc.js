"use strict";

const MIDAS_SUCCESS = 1;
const REFRESH_INTERVAL_MS = 1000;
const APPLY_TIMEOUT_MS = 45000;
const ODB_REQUEST_TIMEOUT_MS = 10000;
const LEGACY_ODB = Object.freeze({
  runState: "/Runinfo/State",
  clients: "/System/Clients",
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
let ODB = {...LEGACY_ODB};
let PATHS = Object.values(ODB);
const EXTRA_FIELDS = [
  ["probe1Enabled", "Probe/ASIC1/Enable"], ["probe1Channel", "Probe/ASIC1/Channel"], ["probe1Signal", "Probe/ASIC1/Signal"],
  ["probe2Enabled", "Probe/ASIC2/Enable"], ["probe2Channel", "Probe/ASIC2/Channel"], ["probe2Signal", "Probe/ASIC2/Signal"],
  ["pattern", "SelectableLogic/Pattern"], ["oneChannel", "SelectableLogic/OneChannel"],
  ["hitThreshold", "SelectableLogic/HitNumThreshold"], ["andChannels", "SelectableLogic/AndChannels"],
  ["triggerWidth", "TriggerWidth"]
];
const EXTRA_DEFAULTS = {probe1Enabled:false, probe1Channel:0, probe1Signal:4,
  probe2Enabled:false, probe2Channel:0, probe2Signal:4, pattern:3, oneChannel:0,
  hitThreshold:1, andChannels:Array(64).fill(false), triggerWidth:118};
const DEVICE_ROOT = "/Equipment/EASIROC/Devices";
const LEGACY_IP = "/Equipment/EASIROC/Settings/Network/IPAddress";
function selectPaths(base) {
  ODB = Object.fromEntries(Object.entries(LEGACY_ODB).map(([key, path]) => [key,
    path.replace("/Equipment/EASIROC", base)]));
  ODB.runState = LEGACY_ODB.runState;
  ODB.transition = "/Runinfo/Transition in progress";
  EXTRA_FIELDS.forEach(([key, path]) => { ODB[key] = `${base}/Settings/${path}`; });
  PATHS = Object.values(ODB);
  SETTINGS_PATHS = ASIC_SETTINGS_KEYS.map(key => ODB[key]);
  if (state.extended) SETTINGS_PATHS.push(...EXTRA_FIELDS.map(([key]) => ODB[key]));
}
const ASIC_SETTINGS_KEYS = ["asic1Code", "asic1Slope", "asic1HgFeedback", "asic1LgFeedback", "asic1HgShaping", "asic1LgShaping", "asic1Input", "asic1ChannelEnabled",
  "asic2Code", "asic2Slope", "asic2HgFeedback", "asic2LgFeedback", "asic2HgShaping", "asic2LgShaping", "asic2Input", "asic2ChannelEnabled"];
let SETTINGS_PATHS = [ODB.asic1Code, ODB.asic1Slope, ODB.asic1HgFeedback, ODB.asic1LgFeedback, ODB.asic1HgShaping, ODB.asic1LgShaping, ODB.asic1Input,
  ODB.asic1ChannelEnabled, ODB.asic2Code, ODB.asic2Slope, ODB.asic2HgFeedback, ODB.asic2LgFeedback, ODB.asic2HgShaping, ODB.asic2LgShaping, ODB.asic2Input, ODB.asic2ChannelEnabled];
const FEEDBACK_OPTIONS = Object.freeze([[0, 0], [100, 8], [200, 4], [300, 12], [400, 2], [500, 10], [600, 6], [700, 14], [800, 1], [900, 9], [1000, 5], [1100, 13], [1200, 3], [1300, 11], [1400, 7], [1500, 15]]);
const SHAPING_OPTIONS = Object.freeze([[25, 1], [50, 2], [75, 3], [100, 4], [125, 5], [150, 6], [175, 7]]);
const TERMINAL_STATES = new Set(["Succeeded", "Failed", "Rejected", "Indeterminate"]);
const state = {values: {}, loaded: null, staged: null, mock: false, scenario: "match", saving: false, saveEpoch: 0, applyPending: false, requestedId: null, selected: null, reading: false, extended: false, selectionEpoch: 0, statusUnavailable: false, unconfirmedRequests: {}};

const element = id => document.getElementById(id);
const present = value => value !== null && value !== undefined;
const truth = value => value === true || Number(value) === 1;
const boolText = value => present(value) ? truth(value) ? "TRUE" : "FALSE" : "—";
const integer = value => present(value) && String(value).trim() !== "" && Number.isInteger(Number(value)) ? Number(value) : NaN;
const runState = value => Number(value) === 1 ? "STOPPED" : Number(value) === 2 ? "PAUSED" : Number(value) === 3 ? "RUNNING" : "—";
const statusValue = key => state.values[ODB[key]];
const setText = (id, value, fallback = "—") => { if (element(id)) element(id).textContent = present(value) && value !== "" ? String(value) : fallback; };
const clone = value => JSON.parse(JSON.stringify(value));

function settingsFromValues(values) {
  const input = path => Array.isArray(values[path]) && values[path].length === 32 ? values[path].map(Number) : null;
  const enabled = path => Array.isArray(values[path]) && values[path].length === 32 ? values[path].map(truth) : null;
  const asic = (code, slope, hgFeedback, lgFeedback, hgShaping, lgShaping, dac, channelEnabled) => ({code: integer(values[code]), slope: integer(values[slope]), hgFeedback: integer(values[hgFeedback]), lgFeedback: integer(values[lgFeedback]), hgShaping: integer(values[hgShaping]), lgShaping: integer(values[lgShaping]), input: input(dac), channelEnabled: enabled(channelEnabled)});
  const extras = Object.fromEntries(EXTRA_FIELDS.map(([key]) => [key,
    present(values[ODB[key]]) ? clone(values[ODB[key]]) : state.extended ? null : clone(EXTRA_DEFAULTS[key])]));
  return {extras, asic1: asic(ODB.asic1Code, ODB.asic1Slope, ODB.asic1HgFeedback, ODB.asic1LgFeedback, ODB.asic1HgShaping, ODB.asic1LgShaping, ODB.asic1Input, ODB.asic1ChannelEnabled), asic2: asic(ODB.asic2Code, ODB.asic2Slope, ODB.asic2HgFeedback, ODB.asic2LgFeedback, ODB.asic2HgShaping, ODB.asic2LgShaping, ODB.asic2Input, ODB.asic2ChannelEnabled)};
}

function settingsAvailable(settings) {
  return [settings.asic1, settings.asic2].every(asic => Number.isInteger(asic.code) && Number.isInteger(asic.slope) && Number.isInteger(asic.hgFeedback) && Number.isInteger(asic.lgFeedback) && Number.isInteger(asic.hgShaping) && Number.isInteger(asic.lgShaping) && Array.isArray(asic.input) && asic.input.length === 32 && Array.isArray(asic.channelEnabled) && asic.channelEnabled.length === 32);
}

function sameSettings(left, right) {
  if (!left || !right) return false;
  return JSON.stringify(left.extras) === JSON.stringify(right.extras) && ["asic1", "asic2"].every(name => left[name].code === right[name].code && left[name].slope === right[name].slope && left[name].hgFeedback === right[name].hgFeedback && left[name].lgFeedback === right[name].lgFeedback && left[name].hgShaping === right[name].hgShaping && left[name].lgShaping === right[name].lgShaping && left[name].input.length === right[name].input.length && left[name].input.every((value, index) => value === right[name].input[index]) && left[name].channelEnabled.length === right[name].channelEnabled.length && left[name].channelEnabled.every((value, index) => value === right[name].channelEnabled[index]));
}

function validateSettings(settings) {
  if (!settingsAvailable(settings)) return "Incomplete ASIC settings";
  if (!settings.extras) return "Incomplete extended settings";
  const e = settings.extras;
  const range = (value, min, max) => Number.isInteger(value) && value >= min && value <= max;
  for (const chip of [1, 2]) {
    if (typeof e[`probe${chip}Enabled`] !== "boolean" || !range(e[`probe${chip}Channel`],0,31) || !range(e[`probe${chip}Signal`],0,4)) return "Invalid Probe settings";
  }
  if (!range(e.pattern,0,9) || !range(e.oneChannel,0,63) || !range(e.hitThreshold,0,64) ||
      !Array.isArray(e.andChannels) || e.andChannels.length !== 64 || !e.andChannels.every(v => typeof v === "boolean")) return "Invalid SelectableLogic settings";
  if (e.triggerWidth !== 0 && (!range(e.triggerWidth,46,278) || (e.triggerWidth-46)%8)) return "Invalid TriggerWidth";
  for (const [name, asic] of [["ASIC1", settings.asic1], ["ASIC2", settings.asic2]]) {
    if (!Number.isInteger(asic.code) || asic.code < 0 || asic.code > 1023) return `${name} Discriminator DAC Code must be 0..1023`;
    if (asic.slope !== 0 && asic.slope !== 1) return `${name} Discriminator Slope must be 0 or 1`;
    if (!FEEDBACK_OPTIONS.some(([femtofarads]) => femtofarads === asic.hgFeedback)) return `${name} HG Feedback Capacitance must be a supported option`;
    if (!FEEDBACK_OPTIONS.some(([femtofarads]) => femtofarads === asic.lgFeedback)) return `${name} LG Feedback Capacitance must be a supported option`;
    if (!SHAPING_OPTIONS.some(([nanoseconds]) => nanoseconds === asic.hgShaping)) return `${name} HG Shaping Time must be a supported option`;
    if (!SHAPING_OPTIONS.some(([nanoseconds]) => nanoseconds === asic.lgShaping)) return `${name} LG Shaping Time must be a supported option`;
    for (let channel = 0; channel < 32; ++channel) if (!Number.isInteger(asic.input[channel]) || asic.input[channel] < 256 || asic.input[channel] > 511) return `${name} InputDAC ch ${channel} must be 256..511`;
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
    const input = document.createElement("input"); input.id = `${asic}-input-${channel}`; input.type = "number"; input.min = "256"; input.max = "511"; input.step = "1"; input.value = String(value);
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
  syncExtraInputs();
  ["asic1", "asic2"].forEach(asic => {
    element(`${asic}-dac-code`).value = String(state.staged[asic].code);
    element(`${asic}-dac-slope`).value = String(state.staged[asic].slope);
    ["hg", "lg"].forEach(gain => { const select = element(`${asic}-${gain}-feedback`); populateFeedbackSelect(select); select.value = String(state.staged[asic][`${gain}Feedback`]); });
    ["hg", "lg"].forEach(gain => { const select = element(`${asic}-${gain}-shaping`); populateShapingSelect(select); select.value = String(state.staged[asic][`${gain}Shaping`]); });
  });
  buildInputGrid();
  buildChannelMaskGrids();
}

function editAllowed() { return runState(statusValue("runState")) === "STOPPED"; }
function statusAvailable() { return ["state", "inProgress", "handled", "successful", "active", "request"].every(key => present(statusValue(key))); }
function unsaved() { return !sameSettings(state.staged, state.loaded); }

// Registration is necessary, but is not proof that the frontend is responsive.
function frontendPresent() {
  const clients = statusValue("clients");
  return clients && typeof clients === "object" && Object.values(clients).some(client =>
    client && typeof client === "object" && (client.name ?? client.Name) === "feeasiroc");
}

function applyBlockReason() {
  if (state.statusUnavailable || !statusAvailable()) return "Apply status unavailable; READ/refresh required. Re-Apply blocked.";
  const request = integer(statusValue("request")), handled = integer(statusValue("handled")), active = integer(statusValue("active"));
  if (![request, handled, active].every(id => Number.isInteger(id) && id >= 0 && id <= 0xffffffff))
    return "Apply request IDs unknown. Re-Apply blocked.";
  if (truth(statusValue("inProgress")) || active !== 0 || request > handled ||
      ["Pending", "Applying"].includes(statusValue("state")))
    return `Apply pending/in progress; completion must be acknowledged before re-Apply.${frontendPresent() ? "" : " feeasiroc is not registered."}`;
  if (statusValue("state") !== "Idle" && !TERMINAL_STATES.has(statusValue("state")))
    return "Apply state unknown. Re-Apply blocked.";
  if (handled < (state.unconfirmedRequests[state.selected?.base] || 0))
    return "Apply submission/completion unknown. Re-Apply blocked until frontend acknowledgement.";
  if (!frontendPresent()) return "feeasiroc is not registered; Apply unavailable.";
  return "";
}

function updateUi() {
  if (!state.staged || !state.selected) return;
  const run = runState(statusValue("runState"));
  const isUnsaved = unsaved();
  const locked = !editAllowed() || state.reading || state.applyPending;
  const validation = validateSettings(state.staged);
  const blocked = applyBlockReason();
  // A timed-out or orphaned mailbox blocks Apply, not local Desired edits.
  const enabled = truth(statusValue("enabled"));
  setText("run-state", run);
  element("run-state").className = run === "STOPPED" ? "stopped" : "active";
  element("settings-lock").hidden = !locked;
  setText("settings-lock", !editAllowed() ? "Settings are locked unless Run state is STOPPED." : state.applyPending ? "Waiting for Apply acknowledgement; editing will unlock on completion or timeout." : "READ in progress.");
  element("unsaved-state").textContent = isUnsaved ? "Unsaved changes" : "Saved";
  element("unsaved-state").className = `unsaved ${isUnsaved ? "dirty" : "clean"}`;
  document.querySelectorAll(".settings-panel input,.settings-panel select").forEach(input => { input.disabled = locked || state.saving; });
  element("save-button").disabled = locked || state.saving || !settingsAvailable(state.staged) || Boolean(validation);
  element("apply-button").disabled = (!present(statusValue("transition")) || Number(statusValue("transition")) !== 0) || Boolean(validation) || locked || !enabled || Boolean(blocked) || isUnsaved || state.saving;
  setText("save-state", validation || (locked ? "Editing locked" : isUnsaved ? "Unsaved changes" : "Saved"));
  setText("operation-note", `Enabled: ${enabled ? "TRUE" : "FALSE"}; Apply requires STOPPED, no transition and saved valid Settings. HV state does not restrict Apply. HV Enable/SetVoltage/CurrentLimit are excluded.`);
  setText("selected-device", `${state.selected.id} — ${state.selected.ip}`);
  setText("selected-status", `${statusValue("state") || "Unknown"}: ${statusValue("applyError") || ""}${blocked ? ` ${blocked}` : ""}${truth(statusValue("indeterminate")) ? " HardwareStateIndeterminate: START inhibited." : ""}`);
  document.querySelectorAll("#extra-settings input,#extra-settings select,#and-channels input").forEach(input => { input.disabled = locked || state.saving || state.reading || !state.extended; });
  element("read-button").disabled = state.saving || state.applyPending || state.reading;
  element("ip-address").disabled = state.saving || state.applyPending || state.reading;
}

function valuesFromRpc(rpc) {
  const values = {};
  PATHS.forEach((path, index) => { values[path] = rpc.result.status[index] === MIDAS_SUCCESS ? rpc.result.data[index] : null; });
  return values;
}

async function readValues(paths = PATHS) {
  if (state.mock) {
    if (paths === PATHS) state.statusUnavailable = false;
    return Object.fromEntries(paths.map(path => [path, state.values[path]]));
  }
  let rpc;
  try { rpc = await boundedRpc(mjsonrpc_db_get_values(paths), "ODB read"); }
  catch (error) { state.statusUnavailable = true; throw error; }
  const values = {};
  paths.forEach((path, index) => { values[path] = rpc.result.status[index] === MIDAS_SUCCESS ? rpc.result.data[index] : null; });
  if (paths === PATHS) state.statusUnavailable = false;
  return values;
}

function boundedRpc(promise, label, timeout = ODB_REQUEST_TIMEOUT_MS) {
  let timer;
  const expired = new Promise((resolve, reject) => {
    timer = window.setTimeout(() => reject(new Error(`${label} timed out; hardware completion may be unknown`)), timeout);
  });
  return Promise.race([promise, expired]).finally(() => window.clearTimeout(timer));
}

async function refresh() {
  try {
    if (!state.selected || state.reading) return;
    const epoch = state.saveEpoch;
    const selectionEpoch = state.selectionEpoch;
    const values = await readValues();
    if (state.reading || state.saving || epoch !== state.saveEpoch || selectionEpoch !== state.selectionEpoch) return;
    Object.assign(state.values, values);
    const loaded = settingsFromValues(values);
    if (settingsAvailable(loaded)) {
      // READ alone replaces Desired Settings. Polls only update the saved ODB
      // comparison, so remote edits cannot silently replace staged values.
      state.loaded = loaded;
    }
    updateUi();
  } catch (error) { state.statusUnavailable = true; updateUi(); showMessage("error", `Cannot refresh MIDAS status: ${decodeError(error)}`); }
  finally { window.setTimeout(refresh, REFRESH_INTERVAL_MS); }
}

function requireStopped() { if (!editAllowed()) throw new Error("Settings and apply are allowed only while Run state is STOPPED"); }

// Check the current ODB run state before writing another setting.
async function requireStoppedNow() {
  const values = await readValues([ODB.runState]);
  state.values[ODB.runState] = values[ODB.runState];
  requireStopped();
}

// Reconcile saved ODB values after an uncertain save; keep Desired Settings.
async function reloadSettings() {
  const values = await readValues();
  const loaded = settingsFromValues(values);
  if (!settingsAvailable(loaded)) throw new Error("ASIC Settings could not be read from ODB");
  Object.assign(state.values, values); state.loaded = loaded;
}

// Save ASIC settings one key at a time and reconcile the page after any failure.
async function saveSettings() {
  if (state.saving || state.applyPending || state.reading || !state.staged) return;
  let attempted = false;
  try {
    clearMessage();
    state.saving = true; ++state.saveEpoch; updateUi();
    await verifySelection();
    const validation = validateSettings(state.staged); if (validation) throw new Error(validation);
    await requireStoppedNow();
    const values = [state.staged.asic1.code, state.staged.asic1.slope, state.staged.asic1.hgFeedback, state.staged.asic1.lgFeedback, state.staged.asic1.hgShaping, state.staged.asic1.lgShaping, state.staged.asic1.input, state.staged.asic1.channelEnabled, state.staged.asic2.code, state.staged.asic2.slope, state.staged.asic2.hgFeedback, state.staged.asic2.lgFeedback, state.staged.asic2.hgShaping, state.staged.asic2.lgShaping, state.staged.asic2.input, state.staged.asic2.channelEnabled];
    if (state.extended) values.push(...EXTRA_FIELDS.map(([key]) => state.staged.extras[key]));
    for (let index = 0; index < SETTINGS_PATHS.length; ++index) {
      if (index) { await requireStoppedNow(); await verifySelection(); }
      attempted = true;
      if (state.mock) state.values[SETTINGS_PATHS[index]] = clone(values[index]);
      else {
        const rpc = await boundedRpc(mjsonrpc_db_set_value(SETTINGS_PATHS[index], values[index]), "ODB Save");
        if (rpc.result.status[0] !== MIDAS_SUCCESS) throw new Error(`ODB write failed for ${SETTINGS_PATHS[index]}`);
      }
    }
    await requireStoppedNow();
    const readback = await readValues(SETTINGS_PATHS);
    const check = settingsFromValues(readback);
    if (!sameSettings(state.staged, check)) throw new Error("ODB readback did not match all saved Settings");
    await requireStoppedNow();
    Object.assign(state.values, readback); state.loaded = clone(check); state.staged = clone(check); syncSettingInputs();
    showMessage("success", "Saved: ODB readback matches all Desired Settings.");
  } catch (error) {
    let detail = "ODB reread; Desired Settings retained for correction/retry.";
    try { await reloadSettings(); }
    catch (reloadError) {
      state.loaded = null;
      element("save-button").disabled = true; element("apply-button").disabled = true;
      detail = `ODB reread failed: ${decodeError(reloadError)}. Desired Settings retained; READ again to confirm stored Settings.`;
    }
    showMessage("error", `Save failed${attempted ? "; partial save possible" : ""}: ${decodeError(error)}. ${detail}`);
  }
  finally { state.saving = false; ++state.saveEpoch; updateUi(); }
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
    const values = await boundedRpc(readValues(), "Apply acknowledgement", Math.max(1, deadline - Date.now()));
    Object.assign(state.values, values); updateUi();
    const handled = integer(statusValue("handled")), currentState = String(statusValue("state") || "");
    if (handled >= requestId && TERMINAL_STATES.has(currentState) && integer(statusValue("active")) === 0 && statusValue("inProgress") === false) return currentState;
    if (!frontendPresent()) throw new Error("feeasiroc is not registered; request completion unknown. Re-Apply blocked.");
    await new Promise(resolve => window.setTimeout(resolve, 500));
  }
  throw new Error(`Timed out waiting for frontend acknowledgement of ApplyRequestId ${requestId}`);
}

async function applySettings() {
  if (state.applyPending || state.saving || state.reading || !state.selected) return;
  try {
    clearMessage();
    state.applyPending = true; updateUi();
    await verifySelection();
    await requireStoppedNow();
    if ((!present(statusValue("transition")) || Number(statusValue("transition")) !== 0)) throw new Error("Transition active or unknown");
    if (validateSettings(state.staged)) throw new Error(validateSettings(state.staged));
    const fresh = settingsFromValues(await readValues(SETTINGS_PATHS));
    if (!sameSettings(state.loaded, fresh)) throw new Error("ODB Settings changed; READ again before Apply");
    requireStopped();
    if (!truth(statusValue("enabled"))) throw new Error("EASI Enabled must be TRUE");
    if (unsaved()) throw new Error("Save to ODB before Apply to hardware");
    const gates = await readValues();
    Object.assign(state.values, gates);
    requireStopped();
    if (!present(statusValue("transition")) || Number(statusValue("transition")) !== 0)
      throw new Error("Transition active or unknown");
    const blocked = applyBlockReason(); if (blocked) throw new Error(blocked);
    await verifySelection();
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
      EXTRA_FIELDS.forEach(([key,path]) => { state.values[`${state.selected.base}/Variables/ASICSlowControl/LastApplied/${path}`] = clone(state.loaded.extras[key]); });
      showMessage("success", `Mock acknowledged ApplyRequestId ${requestId}: Succeeded (no hardware access).`);
    } else {
      state.unconfirmedRequests[state.selected.base] = requestId;
      const rpc = await boundedRpc(mjsonrpc_db_set_value(ODB.request, requestId), "ApplyRequestId submission");
      if (rpc.result.status[0] !== MIDAS_SUCCESS) {
        delete state.unconfirmedRequests[state.selected.base];
        throw new Error("ApplyRequestId ODB write failed");
      }
      const terminal = await waitForTerminal(requestId);
      showMessage(terminal === "Succeeded" ? "success" : "warning", `Frontend acknowledged ApplyRequestId ${requestId}: ${terminal}. ${statusValue("applyError") || ""}`);
    }
  } catch (error) {
    if (state.requestedId !== null) state.statusUnavailable = true;
    showMessage("error", `Apply request failed: ${decodeError(error)}. Desired Settings retained; re-Apply requires confirmed idle frontend status.`);
  }
  finally { state.applyPending = false; state.requestedId = null; updateUi(); }
}

function decodeError(error) { return typeof mjsonrpc_decode_error === "function" ? mjsonrpc_decode_error(error) : String(error); }

function canonicalIp(ip) {
  return /^(0|[1-9]\d{0,2})(\.(0|[1-9]\d{0,2})){3}$/.test(ip) && ip.split(".").every(v => Number(v)<=255);
}
async function registeredDevices() {
  const values = await readValues([DEVICE_ROOT, LEGACY_IP]);
  const devices = [];
  if (present(values[LEGACY_IP])) devices.push({id:"legacy",ip:values[LEGACY_IP],base:"/Equipment/EASIROC"});
  const registry = values[DEVICE_ROOT];
  if (registry && typeof registry === "object") Object.entries(registry).forEach(([key, device]) => {
    // db_get_values emits lowercase keys and adjacent /name metadata.
    if (key.includes("/")) return;
    const id = registry[`${key}/name`] || key;
    if (!/^[A-Za-z0-9_-]{1,31}$/.test(id) || id.toLowerCase() === "legacy" || !device || typeof device !== "object") throw new Error("Invalid/reserved device ID");
    devices.push({id,ip:device.ipaddress ?? device.IPAddress,base:`${DEVICE_ROOT}/${id}`});
  });
  const ips = new Set();
  for (const device of devices) {
    if (!canonicalIp(device.ip)) throw new Error("Invalid registered IPv4 address");
    if (ips.has(device.ip)) throw new Error(`Duplicate IP: ${device.ip}`);
    ips.add(device.ip);
  }
  return devices;
}
async function verifySelection() {
  if (!state.selected) throw new Error("READ a registered IP first");
  const current = (await registeredDevices()).find(d => d.id === state.selected.id);
  if (!current || current.ip !== state.selected.ip || current.base !== state.selected.base) throw new Error("Device registry changed; READ again");
}
async function readSelectedIp() {
  if (state.saving || state.applyPending || state.reading) return;
  state.reading = true; updateUi();
  const epoch = ++state.selectionEpoch;
  const previous = {selected:state.selected, extended:state.extended};
  try {
    const ip = element("ip-address").value.trim();
    if (!canonicalIp(ip)) throw new Error("Enter a canonical IPv4 address");
    const selected = (await registeredDevices()).find(d => d.ip === ip);
    if (!selected) throw new Error(`Unregistered IP: ${ip}`);
    if (epoch !== state.selectionEpoch) return;
    state.extended = selected.id !== "legacy";
    selectPaths(selected.base);
    const values = await readValues();
    state.extended = state.extended || EXTRA_FIELDS.some(([key]) => present(values[ODB[key]]));
    selectPaths(selected.base);
    if (epoch !== state.selectionEpoch) return;
    const loaded = settingsFromValues(values);
    if (!settingsAvailable(loaded) || EXTRA_FIELDS.some(([key]) => !present(loaded.extras[key])) ||
        !Array.isArray(loaded.extras.andChannels) || loaded.extras.andChannels.length !== 64)
      throw new Error("Registered device Settings are incomplete");
    state.selected = selected; Object.assign(state.values,values);
    state.loaded = loaded; state.staged = clone(loaded); syncSettingInputs(); updateUi();
    showMessage("success", `READ ${selected.ip}: loaded ODB Settings. No hardware access.`);
  } catch (error) {
    if (epoch !== state.selectionEpoch) return;
    state.selected = previous.selected; state.extended = previous.extended;
    selectPaths(previous.selected ? previous.selected.base : "/Equipment/EASIROC");
    showMessage("error", decodeError(error));
  } finally { state.reading = false; updateUi(); }
}
function syncExtraInputs() {
  EXTRA_FIELDS.forEach(([key]) => {
    if (key === "andChannels") return;
    const input = element(`extra-${key}`);
    if (input.type === "checkbox") input.checked = state.staged.extras[key];
    else input.value = String(state.staged.extras[key]);
  });
  const grid = element("and-channels"); grid.replaceChildren();
  state.staged.extras.andChannels.forEach((value,index) => {
    const label = document.createElement("label"); label.className = "mask-cell";
    const input = document.createElement("input"); input.type = "checkbox"; input.checked = value;
    input.addEventListener("change", () => {state.staged.extras.andChannels[index]=input.checked;updateUi();});
    label.append(input,document.createTextNode(`ch ${index}`));grid.append(label);
  });
  setText("schema-note", state.extended ? "All Desired Settings are saved and applied together." : "Legacy ASIC schema: provision complete Probe/SelectableLogic/TriggerWidth fields to include them; ASIC Save/Apply is available.");
}
function setupExtraInputs() {
  const container = element("extra-settings");
  const signals = ["PA High Gain","PA Low Gain","Slow Shaper High Gain","Slow Shaper Low Gain","Fast Shaper"];
  const patterns = ["OneChannel","Or32Upper","Or32Lower","Or64","Or32And","Or16And","And32Upper","And32Lower","And64","And32Or"];
  EXTRA_FIELDS.forEach(([key,path]) => {
    if (key === "andChannels") return;
    const label = document.createElement("label"); label.textContent = path;
    let input;
    if (key.endsWith("Signal") || key === "pattern" || key === "triggerWidth") {
      input=document.createElement("select");
      const options = key.endsWith("Signal") ? signals.map((v,i)=>[i,v]) : key === "pattern" ? patterns.map((v,i)=>[i,v]) : [[0,"Raw"],...Array.from({length:30},(_,i)=>[46+8*i,`${46+8*i} ns`])];
      options.forEach(([value,text])=>{const option=document.createElement("option");option.value=String(value);option.textContent=text;input.append(option);});
    } else {input=document.createElement("input");input.type=key.endsWith("Enabled")?"checkbox":"number";input.min="0";input.max=key==="oneChannel"?"63":key.endsWith("Channel")?"31":"64";input.step="1";}
    input.id=`extra-${key}`; input.disabled=true;
    input.addEventListener("change",()=>{if(!state.staged)return;state.staged.extras[key]=input.type==="checkbox"?input.checked:integer(input.value);updateUi();});
    label.append(input);container.append(label);
  });
}
function selectMockScenario(name) {
  if (state.saving || state.applyPending || state.reading) return;
  const source = window.EASIROC_MOCK_SCENARIOS?.[name]; if (!source) return;
  state.scenario=name; state.values=clone(source);state.extended=true;state.statusUnavailable=false;
  state.values[ODB.clients] = {mock: {name:"feeasiroc"}};
  const first = {id:"moduleA",ip:"192.168.10.40",base:`${DEVICE_ROOT}/moduleA`};
  const second = {id:"moduleB",ip:"192.168.10.41",base:`${DEVICE_ROOT}/moduleB`};
  state.values[LEGACY_IP]="192.168.10.39";
  state.values[DEVICE_ROOT]={moduleA:{IPAddress:first.ip},moduleB:{IPAddress:second.ip}};
  for (const device of [first,second]) {
    for (const [path,value] of Object.entries(source)) state.values[path.replace("/Equipment/EASIROC",device.base)]=clone(value);
    EXTRA_FIELDS.forEach(([key,path])=>{state.values[`${device.base}/Settings/${path}`]=clone(EXTRA_DEFAULTS[key]);});
  }
  state.values[`${second.base}/Settings/ASIC1/DiscriminatorDACCode`]=700;
  state.selected=first;selectPaths(first.base);state.values[ODB.transition]=0;
  state.loaded=settingsFromValues(state.values);state.staged=clone(state.loaded);
  if(name==="unsaved")state.staged.asic1.input[0]+=1;
  element("ip-address").value=first.ip;syncSettingInputs();updateUi();
}

function setupEvents() {
  setupExtraInputs();
  element("read-button").addEventListener("click",readSelectedIp);
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
    mhttpd_init("EASI Slow Control", REFRESH_INTERVAL_MS);
  }
  refresh();
}
