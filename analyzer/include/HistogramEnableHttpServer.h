#ifndef ANA_HISTOGRAM_ENABLE_HTTP_SERVER_H
#define ANA_HISTOGRAM_ENABLE_HTTP_SERVER_H

#include "THttpCallArg.h"
#include "THttpServer.h"

#include <memory>

namespace ana {

// ROOT treats the last component of a URL without a trailing slash as a file.
// Redirect that one browser entry point to the WS handler's directory URL.
bool RedirectHistogramEnableEntry(THttpCallArg& arg);

class HistogramEnableHttpServer final : public THttpServer {
 public:
  using THttpServer::THttpServer;

 protected:
  void ProcessRequest(std::shared_ptr<THttpCallArg> arg) override;
};

}  // namespace ana

#endif
