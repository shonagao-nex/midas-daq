#ifndef ANA_HISTOGRAM_ENABLE_WEB_HANDLER_H
#define ANA_HISTOGRAM_ENABLE_WEB_HANDLER_H

#include "THttpWSHandler.h"

#include <string>

class MVOdb;

namespace ana {

class HistogramEnableWebHandler final : public THttpWSHandler {
 public:
  explicit HistogramEnableWebHandler(MVOdb* odb);

  TString GetDefaultPageContent() override;
  Bool_t ProcessWS(THttpCallArg* arg) override;

 private:
  void SendGroup(UInt_t wsid, const std::string& group);

  MVOdb* odb_ = nullptr;  // Owned by the connected MIDAS client.
};

}  // namespace ana

#endif
