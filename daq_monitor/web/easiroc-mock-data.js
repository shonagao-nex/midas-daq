"use strict";
(function () {
  const p = "/Equipment/EASIROC/";
  const s = p + "Settings/";
  const v = p + "Variables/ASICSlowControl/";
  const input = Array.from({length: 32}, () => 480);
  const channelEnabled = Array.from({length: 32}, () => true);
  const base = {
    "/Runinfo/State": 1,
    [s + "Enabled"]: true, [s + "ASICSlowControl/ApplyAtBOR"]: false,
    [s + "ASIC1/DiscriminatorDACCode"]: 600, [s + "ASIC1/DiscriminatorDACSlope"]: 1, [s + "ASIC1/InputDAC"]: input,
    [s + "ASIC1/HGFeedbackCapacitance"]: 100, [s + "ASIC1/LGFeedbackCapacitance"]: 100,
    [s + "ASIC1/HGShapingTime"]: 100, [s + "ASIC1/LGShapingTime"]: 50,
    [s + "ASIC1/ChannelEnabled"]: channelEnabled,
    [s + "ASIC2/DiscriminatorDACCode"]: 600, [s + "ASIC2/DiscriminatorDACSlope"]: 1, [s + "ASIC2/InputDAC"]: input,
    [s + "ASIC2/HGFeedbackCapacitance"]: 100, [s + "ASIC2/LGFeedbackCapacitance"]: 100,
    [s + "ASIC2/HGShapingTime"]: 100, [s + "ASIC2/LGShapingTime"]: 50,
    [s + "ASIC2/ChannelEnabled"]: channelEnabled,
    [p + "Commands/ASICSlowControl/ApplyRequestId"]: 9,
    [v + "ActiveRequestId"]: 0, [v + "LastHandledRequestId"]: 9, [v + "LastSuccessfulRequestId"]: 9,
    [v + "ApplyState"]: "Succeeded", [v + "ApplyInProgress"]: false, [v + "LastAttemptSucceeded"]: true, [v + "LastApplyError"]: "", [v + "LastApplyUnixTime"]: 1780000000,
    [v + "ConfigurationMatch"]: true, [v + "ConfigurationStatus"]: "Match", [v + "ConfigurationDetail"]: "", [v + "HardwareStateIndeterminate"]: false,
    [v + "LastApplied/Valid"]: true, [v + "LastApplied/RequestId"]: 9, [v + "LastApplied/ApplyUnixTime"]: 1780000000,
    [v + "LastApplied/ASIC1/DiscriminatorDACCode"]: 600, [v + "LastApplied/ASIC1/DiscriminatorDACSlope"]: 1, [v + "LastApplied/ASIC1/InputDAC"]: input,
    [v + "LastApplied/ASIC1/HGFeedbackCapacitance"]: 100, [v + "LastApplied/ASIC1/LGFeedbackCapacitance"]: 100,
    [v + "LastApplied/ASIC1/HGShapingTime"]: 100, [v + "LastApplied/ASIC1/LGShapingTime"]: 50,
    [v + "LastApplied/ASIC1/ChannelEnabled"]: channelEnabled,
    [v + "LastApplied/ASIC2/DiscriminatorDACCode"]: 600, [v + "LastApplied/ASIC2/DiscriminatorDACSlope"]: 1, [v + "LastApplied/ASIC2/InputDAC"]: input,
    [v + "LastApplied/ASIC2/HGFeedbackCapacitance"]: 100, [v + "LastApplied/ASIC2/LGFeedbackCapacitance"]: 100,
    [v + "LastApplied/ASIC2/HGShapingTime"]: 100, [v + "LastApplied/ASIC2/LGShapingTime"]: 50,
    [v + "LastApplied/ASIC2/ChannelEnabled"]: channelEnabled
  };
  const copy = changes => Object.freeze(Object.assign({}, base, changes));
  const hv = (enabled, ramp, measured, current, age=0) => ({
    [p+"Settings/HV/CurrentLimit_uA"]:20,
    [p+"Commands/HV/TargetVoltage_V"]:enabled==="ON"?62:0,
    [p+"Commands/HV/RequestId"]:0,
    [p+"Variables/HV/LastHandledRequestId"]:0,
    [p+"Variables/HV/ActiveRequestId"]:0,
    [p+"Variables/HV/ResultRequestId"]:0,
    [p+"Variables/HV/EnabledState"]:enabled,
    [p+"Variables/HV/RampStatus"]:ramp,
    [p+"Variables/HV/SetVoltageState"]:enabled==="Unknown"?"Unknown":"Known",
    [p+"Variables/HV/SetVoltage_V"]:enabled==="ON"?62:0,
    [p+"Variables/HV/MonitoringStatus"]:enabled==="Unknown"?"Mock communication timeout":current>20?"WARNING: CurrentLimit exceeded (mock)":"OK",
    [p+"Variables/HV/MeasuredVoltage_V"]:measured,
    [p+"Variables/HV/MeasuredCurrent_uA"]:current,
    [p+"Variables/HV/MeasurementValid"]:enabled!=="Unknown",
    [p+"Variables/HV/LastUpdateUnixTime"]:Math.floor(Date.now()/1000)-age,
    [p+"Variables/HV/InProgress"]:ramp==="Settling",
    [p+"Variables/HV/EnableAvailable"]:enabled==="OFF",
    [p+"Variables/HV/VoltageAvailable"]:enabled==="ON" && ramp!=="Settling",
    [p+"Variables/HV/StopAvailable"]:true,
    [p+"Variables/HV/EmergencyAvailable"]:true,
    [p+"Variables/HV/ProtectionActive"]:enabled==="ON",
    [p+"Variables/HV/BlockedReason"]:"MOCK: simulated capabilities; no hardware access",
    [p+"Variables/HV/Result"]:"Idle (mock)",
    [p+"Variables/HV/Error"]:enabled==="Unknown"?"Mock communication timeout":""
  });
  window.EASIROC_MOCK_SCENARIOS = Object.freeze({
    match: copy({}),
    hv_disabled: copy(hv("OFF","Disabled",0.8,1)),
    hv_settling: copy({...hv("ON","Settling",60,4),
      [p+"Commands/HV/TargetVoltage_V"]:0,
      [p+"Variables/HV/SetVoltage_V"]:52,
      [p+"Commands/HV/RequestId"]:1,
      [p+"Variables/HV/ActiveRequestId"]:1,
      [p+"Variables/HV/ResultRequestId"]:1,
      [p+"Variables/HV/Result"]:"Pending"}),
    hv_overcurrent: copy(hv("ON","Steady",62,21)),
    hv_stale: copy(hv("ON","Steady",62,1,66)),
    hv_error: copy(hv("Unknown","Unknown",0,0)),
    mismatch: copy({[v + "ConfigurationMatch"]: false, [v + "ConfigurationStatus"]: "Mismatch", [v + "ConfigurationDetail"]: "ASIC2 InputDAC[17] differs", [s + "ASIC2/InputDAC"]: input.map((x, i) => i === 17 ? 351 : x)}),
    unknown: copy({[v + "ConfigurationMatch"]: false, [v + "ConfigurationStatus"]: "Unknown", [v + "ConfigurationDetail"]: "No successfully applied ASIC slow-control configuration is recorded", [v + "LastApplied/Valid"]: false}),
    indeterminate: copy({[v + "ConfigurationMatch"]: false, [v + "ConfigurationStatus"]: "Indeterminate", [v + "ConfigurationDetail"]: "ASIC slow-control hardware state is indeterminate after an incomplete apply", [v + "HardwareStateIndeterminate"]: true, [v + "ApplyState"]: "Failed", [v + "LastAttemptSucceeded"]: false, [v + "LastApplyError"]: "ASIC slow-control hardware state may be indeterminate after partial apply failure"}),
    applying: copy({[v + "ActiveRequestId"]: 10, [v + "ApplyState"]: "Applying", [v + "ApplyInProgress"]: true}),
    succeeded: copy({[v + "ApplyState"]: "Succeeded"}),
    failed: copy({[v + "ApplyState"]: "Failed", [v + "LastAttemptSucceeded"]: false, [v + "LastApplyError"]: "Mock RBCP write failure"}),
    rejected: copy({[v + "ApplyState"]: "Rejected", [v + "LastAttemptSucceeded"]: false, [v + "LastApplyError"]: "Manual ASIC slow-control apply requires Run state STOPPED"}),
    unsaved: copy({}),
    nondefault_shaping: copy({[s + "ASIC1/HGShapingTime"]: 75}),
    masked_channel: copy({[s + "ASIC1/ChannelEnabled"]: channelEnabled.map((value, index) => index === 17 ? false : value)})
  });
}());
