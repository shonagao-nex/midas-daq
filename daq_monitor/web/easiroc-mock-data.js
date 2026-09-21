"use strict";
(function () {
  const p = "/Equipment/EASIROC/";
  const s = p + "Settings/";
  const v = p + "Variables/ASICSlowControl/";
  const input = Array.from({length: 32}, () => 350);
  const base = {
    "/Runinfo/State": 1,
    [s + "Enabled"]: true, [s + "ASICSlowControl/ApplyAtBOR"]: false,
    [s + "ASIC1/DiscriminatorDACCode"]: 600, [s + "ASIC1/DiscriminatorDACSlope"]: 1, [s + "ASIC1/InputDAC"]: input,
    [s + "ASIC2/DiscriminatorDACCode"]: 600, [s + "ASIC2/DiscriminatorDACSlope"]: 1, [s + "ASIC2/InputDAC"]: input,
    [p + "Commands/ASICSlowControl/ApplyRequestId"]: 9,
    [v + "ActiveRequestId"]: 0, [v + "LastHandledRequestId"]: 9, [v + "LastSuccessfulRequestId"]: 9,
    [v + "ApplyState"]: "Succeeded", [v + "ApplyInProgress"]: false, [v + "LastAttemptSucceeded"]: true, [v + "LastApplyError"]: "", [v + "LastApplyUnixTime"]: 1780000000,
    [v + "ConfigurationMatch"]: true, [v + "ConfigurationStatus"]: "Match", [v + "ConfigurationDetail"]: "", [v + "HardwareStateIndeterminate"]: false,
    [v + "LastApplied/Valid"]: true, [v + "LastApplied/RequestId"]: 9, [v + "LastApplied/ApplyUnixTime"]: 1780000000,
    [v + "LastApplied/ASIC1/DiscriminatorDACCode"]: 600, [v + "LastApplied/ASIC1/DiscriminatorDACSlope"]: 1, [v + "LastApplied/ASIC1/InputDAC"]: input,
    [v + "LastApplied/ASIC2/DiscriminatorDACCode"]: 600, [v + "LastApplied/ASIC2/DiscriminatorDACSlope"]: 1, [v + "LastApplied/ASIC2/InputDAC"]: input
  };
  const copy = changes => Object.freeze(Object.assign({}, base, changes));
  window.EASIROC_MOCK_SCENARIOS = Object.freeze({
    match: copy({}),
    mismatch: copy({[v + "ConfigurationMatch"]: false, [v + "ConfigurationStatus"]: "Mismatch", [v + "ConfigurationDetail"]: "ASIC2 InputDAC[17] differs", [s + "ASIC2/InputDAC"]: input.map((x, i) => i === 17 ? 351 : x)}),
    unknown: copy({[v + "ConfigurationMatch"]: false, [v + "ConfigurationStatus"]: "Unknown", [v + "ConfigurationDetail"]: "No successfully applied ASIC slow-control configuration is recorded", [v + "LastApplied/Valid"]: false}),
    indeterminate: copy({[v + "ConfigurationMatch"]: false, [v + "ConfigurationStatus"]: "Indeterminate", [v + "ConfigurationDetail"]: "ASIC slow-control hardware state is indeterminate after an incomplete apply", [v + "HardwareStateIndeterminate"]: true, [v + "ApplyState"]: "Failed", [v + "LastAttemptSucceeded"]: false, [v + "LastApplyError"]: "ASIC slow-control hardware state may be indeterminate after partial apply failure"}),
    applying: copy({[v + "ActiveRequestId"]: 10, [v + "ApplyState"]: "Applying", [v + "ApplyInProgress"]: true}),
    succeeded: copy({[v + "ApplyState"]: "Succeeded"}),
    failed: copy({[v + "ApplyState"]: "Failed", [v + "LastAttemptSucceeded"]: false, [v + "LastApplyError"]: "Mock RBCP write failure"}),
    rejected: copy({[v + "ApplyState"]: "Rejected", [v + "LastAttemptSucceeded"]: false, [v + "LastApplyError"]: "Manual ASIC slow-control apply requires Run state STOPPED"}),
    unsaved: copy({})
  });
}());
