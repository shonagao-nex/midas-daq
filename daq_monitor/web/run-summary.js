"use strict";

const RUN_SUMMARY_BATCH = 10;
const RUN_SUMMARY_CONCURRENCY = 4;
let targetRuns = [];
let nextTargetIndex = 0;
let displayedRuns = 0;
let loadingRuns = false;
let exportingRuns = false;

function runSummaryInteger(value) {
  if (value === null || value === undefined || value === "") return "N/A";
  if (typeof value === "number") return Number.isSafeInteger(value) ? String(value) : "N/A";
  if (typeof value !== "string" || !/^-?(?:0x[0-9a-f]+|[0-9]+)$/i.test(value.trim())) return "N/A";
  try { return BigInt(value.trim()).toString(10); } catch (_) { return "N/A"; }
}

function runSummaryEvent(value) {
  const count = runSummaryInteger(value);
  return count === "-1" ? "N/A" : count;
}

function runSummaryScaler(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) return [];
  return Object.entries(value)
    .filter(([name]) => /^ch(?:0[0-9]|[1-5][0-9]|6[0-3])$/.test(name))
    .sort(([a], [b]) => Number(a.slice(2)) - Number(b.slice(2)));
}

function summaryText(value) {
  return value === null || value === undefined || value === "" ? "N/A" : String(value);
}

function summaryTime(value) {
  const original = summaryText(value);
  const match = /^[A-Za-z]{3} ([A-Za-z]{3})\s+(\d{1,2}) (\d{2}):(\d{2}):(\d{2}) (\d{4})$/.exec(original);
  if (!match) return original;
  const month = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"].indexOf(match[1]) + 1;
  if (!month) return original;
  return `${match[6]}/${String(month).padStart(2, "0")}/${match[2].padStart(2, "0")} ${match[3]}:${match[4]}:${match[5]}`;
}

function summaryCell(row, value) {
  const cell = row.insertCell();
  cell.textContent = value;
  return cell;
}

function normalizedRunSummary(number, record) {
  const bor = record && typeof record.BOR === "object" && record.BOR || {};
  const eor = record && typeof record.EOR === "object" && record.EOR || null;
  const numeric = value => {
    const parsed = runSummaryInteger(value);
    return parsed === "N/A" ? null : parsed;
  };
  const time = value => {
    const parsed = summaryTime(value);
    return parsed === "N/A" ? null : parsed;
  };
  return {
    run: numeric(bor["Run number"]) || String(number),
    type: bor.Type ?? null,
    start: time(bor["Start time"]),
    stop: eor ? time(eor["Stop time"]) : null,
    duration: eor ? numeric(eor.Duration) : null,
    vme: eor ? numeric(eor["VME events"]) : null,
    easi: eor ? numeric(eor["EASIROC events"]) : null,
    hul: eor ? numeric(eor["HUL events"]) : null,
    slip: eor ? numeric(eor.EventSlipCount) : null,
    status: eor ? (eor["DAQ Status"] ?? null) : "INCOMPLETE",
    statusSummary: eor ? (eor["DAQ Summary"] ?? null) : null,
    comment: eor ? (eor.Comment ?? null) : null,
    scaler: eor ? runSummaryScaler(eor["Scaler 64ch"]).map(
      ([name, value]) => [name, numeric(value)]) : []
  };
}

function renderRunSummaryRow(number, record) {
  const data = normalizedRunSummary(number, record);
  const row = document.createElement("tr");
  summaryCell(row, data.run);
  summaryCell(row, summaryText(data.type));
  summaryCell(row, summaryText(data.start));
  summaryCell(row, summaryText(data.stop));
  summaryCell(row, data.duration === null ? "N/A" : `${data.duration} s`);
  for (const count of [data.vme, data.easi, data.hul])
    summaryCell(row, count === null ? "N/A" : runSummaryEvent(count));
  summaryCell(row, summaryText(data.slip));
  const statusCell = row.insertCell();
  const status = summaryText(data.status);
  const badge = document.createElement("span");
  badge.className = "status-badge " + ({OK:"state-ok", WARNING:"state-warning", ERROR:"state-error", INCOMPLETE:"state-incomplete"}[status] || "state-unknown");
  badge.textContent = status;
  badge.title = status === "INCOMPLETE" ? "End of run has not been recorded" : summaryText(data.statusSummary);
  statusCell.append(badge);
  const commentCell = row.insertCell();
  const comment = summaryText(data.comment);
  if (comment.length > 80) {
    const details = document.createElement("details");
    const teaser = document.createElement("summary");
    teaser.textContent = `${comment.slice(0, 77)}…`;
    const full = document.createElement("p");
    full.textContent = comment;
    details.append(teaser, full);
    commentCell.append(details);
  } else commentCell.textContent = comment;
  const scalerCell = row.insertCell();
  const channels = data.scaler;
  if (!channels.length) scalerCell.textContent = "N/A";
  else {
    const details = document.createElement("details");
    const teaser = document.createElement("summary");
    teaser.textContent = `${channels.length} ch`;
    const list = document.createElement("dl");
    list.className = "scaler-list";
    for (const [name, value] of channels) {
      const term = document.createElement("dt"), data = document.createElement("dd");
      term.textContent = name;
      data.textContent = summaryText(value);
      list.append(term, data);
    }
    details.append(teaser, list);
    scalerCell.append(details);
  }
  return row;
}

async function fetchRunSummary(number) {
  const filename = `/runlogs/runlog_${String(number).padStart(6, "0")}.json`;
  const response = await fetch(filename, {cache: "no-store"});
  // mhttpd returns 400 for a missing file under /Custom/Path.
  if (response.status === 400 || response.status === 404) return null;
  if (!response.ok) throw new Error(`Run #${number}: HTTP ${response.status}`);
  const content = await response.text();
  try { return JSON.parse(content); }
  catch (failure) {
    // MIDAS writes BOR followed by a comma and adds EOR at STOP. A run
    // interrupted before EOR therefore has this unfinished JSON envelope.
    if (content.trimEnd().endsWith(",")) {
      const partial = JSON.parse(content.trimEnd().slice(0, -1) + "\n}");
      if (partial && partial.BOR && !partial.EOR) return partial;
    }
    throw failure;
  }
}

function validRunList(value) {
  return value && Array.isArray(value.runs) &&
    value.runs.every(number => Number.isSafeInteger(number) && number > 0);
}

async function loadRunTargets() {
  const indexResponse = await fetch("/runlogs/runlog_index.json", {cache: "no-store"});
  if (!indexResponse.ok) throw new Error(`Cannot read Runlog index: HTTP ${indexResponse.status}`);
  const index = await indexResponse.json();
  if (!validRunList(index)) throw new Error("Runlog index has an invalid runs array");
  const available = new Set(index.runs);
  let selected = index.runs;
  try {
    const selectionResponse = await fetch("/runlogs/runlog_selection.json", {cache: "no-store"});
    if (!selectionResponse.ok) throw new Error(`HTTP ${selectionResponse.status}`);
    const selection = await selectionResponse.json();
    if (!validRunList(selection)) throw new Error("invalid runs array");
    if (selection.runs.length)
      selected = selection.runs.filter(number => available.has(number));
  } catch (failure) {
    const warning = document.getElementById("summary-error");
    warning.textContent = `Ignoring unreadable Runlog selection (${failure.message}); showing index runs.`;
    warning.hidden = false;
  }
  return [...new Set(selected)].sort((a, b) => b - a);
}

const CSV_COLUMNS = ["Run", "Type", "Start", "Stop", "Duration", "VME",
  "EASI", "HUL", "Slip", "Status", "Comment",
  ...Array.from({length: 64}, (_, channel) => `Scaler${String(channel).padStart(2, "0")}`)];

function csvQuote(value) {
  const text = value === null || value === undefined ? "" : String(value);
  return /[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

function csvRunSummaryRow(number, record) {
  const data = normalizedRunSummary(number, record);
  const channels = new Map(data.scaler);
  const values = [data.run, data.type, data.start, data.stop, data.duration,
    data.vme, data.easi, data.hul, data.slip, data.status, data.comment,
    ...Array.from({length: 64}, (_, channel) =>
      channels.get(`ch${String(channel).padStart(2, "0")}`) ?? null)];
  return values.map(csvQuote).join(",");
}

function csvFilename(date) {
  const two = value => String(value).padStart(2, "0");
  return `run_summary_${date.getFullYear()}${two(date.getMonth() + 1)}${two(date.getDate())}_${two(date.getHours())}${two(date.getMinutes())}${two(date.getSeconds())}.csv`;
}

async function exportRunSummaryCsv() {
  if (exportingRuns) return;
  exportingRuns = true;
  const button = document.getElementById("export-csv");
  const status = document.getElementById("export-status");
  button.disabled = true;
  status.hidden = false;
  let skipped = 0, exported = 0;
  try {
    const lines = [CSV_COLUMNS.map(csvQuote).join(",")];
    for (let first = 0; first < targetRuns.length; first += 50) {
      const numbers = targetRuns.slice(first, first + 50);
      const records = new Array(numbers.length);
      let cursor = 0;
      await Promise.all(Array.from({length: Math.min(RUN_SUMMARY_CONCURRENCY, numbers.length)}, async () => {
        while (cursor < numbers.length) {
          const index = cursor++;
          try { records[index] = await fetchRunSummary(numbers[index]); }
          catch (_) { records[index] = null; skipped++; }
        }
      }));
      for (let i = 0; i < numbers.length; i++) {
        if (records[i] === null) continue;
        lines.push(csvRunSummaryRow(numbers[i], records[i]));
        exported++;
      }
      status.textContent = `Exporting Runlogs: checked ${Math.min(first + 50, targetRuns.length)} of ${targetRuns.length}…`;
    }
    const blob = new Blob(["\uFEFF", lines.join("\r\n") + "\r\n"],
      {type: "text/csv;charset=utf-8"});
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = csvFilename(new Date());
    document.body.append(link);
    link.click();
    link.remove();
    window.setTimeout(() => URL.revokeObjectURL(url), 60000);
    status.textContent = `Exported ${exported} runs${skipped ? `; skipped ${skipped} unreadable files` : ""}.`;
  } catch (failure) {
    status.textContent = `CSV export failed: ${failure.message}`;
  } finally {
    button.disabled = false;
    exportingRuns = false;
  }
}

async function loadOlderRuns() {
  if (loadingRuns || nextTargetIndex >= targetRuns.length) return;
  loadingRuns = true;
  const button = document.getElementById("older-runs");
  const body = document.getElementById("run-summary-rows");
  const error = document.getElementById("summary-error");
  button.disabled = true;
  const numbers = targetRuns.slice(nextTargetIndex, nextTargetIndex + RUN_SUMMARY_BATCH);
  const results = new Array(numbers.length);
  let cursor = 0;
  await Promise.all(Array.from({length: Math.min(RUN_SUMMARY_CONCURRENCY, numbers.length)}, async () => {
    while (cursor < numbers.length) {
      const index = cursor++;
      try { results[index] = await fetchRunSummary(numbers[index]); }
      catch (failure) { results[index] = null; error.textContent = `Some Runlogs could not be read: ${failure.message}`; error.hidden = false; }
    }
  }));
  if (!displayedRuns) body.replaceChildren();
  for (let i = 0; i < results.length; i++) {
    if (results[i] === null) continue;
    body.append(renderRunSummaryRow(numbers[i], results[i]));
    displayedRuns++;
  }
  nextTargetIndex += numbers.length;
  if (!displayedRuns) body.innerHTML = '<tr><td colspan="12">No Runlog files found in this range.</td></tr>';
  document.getElementById("summary-count").textContent = `${displayedRuns} runs shown`;
  button.disabled = nextTargetIndex >= targetRuns.length;
  loadingRuns = false;
}

async function initializeRunSummary() {
  mhttpd_init("Run Summary");
  document.getElementById("older-runs").addEventListener("click", loadOlderRuns);
  document.getElementById("export-csv").addEventListener("click", exportRunSummaryCsv);
  try {
    targetRuns = await loadRunTargets();
    if (targetRuns.length) await loadOlderRuns();
    else {
      document.getElementById("run-summary-rows").innerHTML = '<tr><td colspan="12">No Runlogs selected.</td></tr>';
      document.getElementById("summary-count").textContent = "0 runs shown";
    }
  } catch (failure) {
    const error = document.getElementById("summary-error");
    error.textContent = `Cannot load Run Summary: ${failure.message}`;
    error.hidden = false;
    document.getElementById("run-summary-rows").innerHTML = '<tr><td colspan="12">Run Summary unavailable.</td></tr>';
  }
}
