#include "HistogramEnableWebHandler.h"

#include "HistogramEnableControl.h"
#include "THttpCallArg.h"

#include <sstream>
#include <string>

namespace ana {

HistogramEnableWebHandler::HistogramEnableWebHandler(MVOdb* odb)
    : THttpWSHandler("HistogramEnable", "Histogram channel enable editor"),
      odb_(odb) {}

TString HistogramEnableWebHandler::GetDefaultPageContent() {
  return ANA_HISTOGRAM_ENABLE_PAGE;
}

void HistogramEnableWebHandler::SendGroup(UInt_t wsid,
                                          const std::string& group) {
  HistogramEnableGroup state;
  std::string error;
  if (!HistogramEnableControl::Read(odb_, group, &state, &error)) {
    SendCharStarWS(wsid, ("ERROR\t" + group + "\t" + error).c_str());
    return;
  }
  std::string bits;
  bits.reserve(state.enabled.size());
  for (bool value : state.enabled) bits += value ? '1' : '0';
  SendCharStarWS(wsid, ("STATE\t" + group + "\t" + bits).c_str());
}

Bool_t HistogramEnableWebHandler::ProcessWS(THttpCallArg* arg) {
  if (!arg || !arg->GetWSId()) return kFALSE;
  if (arg->IsMethod("WS_CONNECT") || arg->IsMethod("WS_CLOSE"))
    return kTRUE;
  if (arg->IsMethod("WS_READY")) {
    for (const auto& name : HistogramEnableControl::GroupNames())
      SendGroup(arg->GetWSId(), name);
    return kTRUE;
  }
  if (!arg->IsMethod("WS_DATA")) return kFALSE;
  if (arg->GetPostDataLength() < 0 || arg->GetPostDataLength() > 128) {
    SendCharStarWS(arg->GetWSId(), "ERROR\trequest\tInvalid request");
    return kTRUE;
  }
  const std::string request(
      static_cast<const char*>(arg->GetPostData()),
      static_cast<std::size_t>(arg->GetPostDataLength()));
  if (request == "GET") {
    for (const auto& name : HistogramEnableControl::GroupNames())
      SendGroup(arg->GetWSId(), name);
    return kTRUE;
  }

  std::istringstream input(request);
  std::string action, group, value, trailing;
  std::size_t channel = 0;
  if (!(input >> action >> group >> channel >> value) ||
      input >> trailing || action != "SET" ||
      (value != "0" && value != "1")) {
    SendCharStarWS(arg->GetWSId(), "ERROR\trequest\tInvalid request");
    return kTRUE;
  }
  std::string error;
  if (!HistogramEnableControl::Set(odb_, group, channel, value == "1",
                                   &error)) {
    SendCharStarWS(arg->GetWSId(), ("ERROR\t" + group + "\t" + error).c_str());
    return kTRUE;
  }
  SendGroup(arg->GetWSId(), group);
  return kTRUE;
}

}  // namespace ana
