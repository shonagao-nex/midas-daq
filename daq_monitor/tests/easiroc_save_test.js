"use strict";

const GLib = imports.gi.GLib;
const ByteArray = imports.byteArray;
const [ok, bytes] = GLib.file_get_contents("web/easiroc.js");
if (!ok) throw new Error("Cannot read web/easiroc.js");

const test = new Function(ByteArray.toString(bytes) + `
  const nodes = {};
  const document = {getElementById: id => nodes[id] || (nodes[id] = {disabled: false})};
  let syncs = 0;
  syncSettingInputs = () => { ++syncs; };
  updateUi = () => {};
  const saved = [100, 1, 100, 100, 25, 25, Array(32).fill(0), Array(32).fill(true),
    200, 0, 100, 100, 25, 25, Array(32).fill(0), Array(32).fill(true)];
  let db, writes, failAt, changeAt, reads;

  function assert(condition, message) { if (!condition) throw new Error(message); }
  function copy(value) { return value === undefined ? null : JSON.parse(JSON.stringify(value)); }
  function reset() {
    db = {[ODB.runState]: 1};
    SETTINGS_PATHS.forEach((path, index) => { db[path] = copy(saved[index]); });
    state.values = copy(db); state.loaded = settingsFromValues(db); state.staged = copy(state.loaded);
    state.staged.asic1.code = 101; state.saving = false; state.saveEpoch = 0;
    writes = 0; reads = 0; failAt = 0; changeAt = 0; syncs = 0;
    nodes.message = {hidden: true, textContent: ""};
  }
  async function mjsonrpc_db_get_values(paths) {
    ++reads;
    return {result: {status: paths.map(() => 1), data: paths.map(path => copy(db[path]))}};
  }
  async function mjsonrpc_db_set_value(path, value) {
    assert(SETTINGS_PATHS.includes(path), "save wrote a non-Settings ODB key");
    ++writes;
    if (writes === failAt) return {result: {status: [0]}};
    db[path] = copy(value);
    if (writes === changeAt) db[ODB.runState] = 3;
    return {result: {status: [1]}};
  }

  return (async () => {
    reset();
    await saveSettings();
    assert(writes === SETTINGS_PATHS.length, "normal save did not write all keys");
    assert(state.loaded.asic1.code === 101 && state.staged.asic1.code === 101, "normal save did not update loaded settings");
    assert(nodes.message.textContent.includes("Saved: ODB readback"), "normal save message missing");

    reset(); failAt = 1;
    await saveSettings();
    assert(writes === 1 && reads > 1 && syncs > 0, "first write failure did not reread ODB and refresh inputs");
    assert(state.staged.asic1.code === 100 && state.loaded.asic1.code === 100, "first failure left edited settings on screen");
    assert(nodes.message.textContent.includes("partial save possible"), "first failure warning missing");

    reset(); failAt = 8;
    await saveSettings();
    assert(writes === 8 && db[ODB.asic1Code] === 101, "middle failure did not leave expected partial ODB state");
    assert(state.staged.asic1.code === 101 && state.loaded.asic1.code === 101 && syncs > 0, "middle failure did not show actual ODB state");
    assert(nodes.message.textContent.includes("partial save possible"), "middle failure warning missing");

    reset(); changeAt = 3;
    await saveSettings();
    assert(writes === 3 && db[ODB.runState] === 3, "run change did not stop later writes");
    assert(state.staged.asic1.code === 101 && state.values[ODB.runState] === 3, "run change did not reload ODB");
    assert(nodes.message.textContent.includes("partial save possible"), "run change warning missing");

    reset(); changeAt = SETTINGS_PATHS.length;
    await saveSettings();
    assert(writes === SETTINGS_PATHS.length && state.values[ODB.runState] === 3, "last-write run change was missed");
    assert(nodes.message.textContent.includes("partial save possible"), "last-write run change warning missing");

    reset(); db[ODB.runState] = 3;
    await saveSettings();
    assert(writes === 0 && state.staged.asic1.code === 100, "preflight did not block and reload");
    assert(!nodes.message.textContent.includes("partial save possible"), "preflight falsely warned of partial save");
    print("easiroc_save_test: passed");
  })();
`);

test().catch(error => { printerr(String(error)); printerr(error.stack || ""); imports.system.exit(1); });
