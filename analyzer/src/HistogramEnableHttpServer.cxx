#include "HistogramEnableHttpServer.h"

#include <cstring>

namespace ana {

bool RedirectHistogramEnableEntry(THttpCallArg& arg) {
  if (std::strcmp(arg.GetMethod(), "GET") != 0 ||
      std::strcmp(arg.GetPathName(), "Analyzer") != 0 ||
      std::strcmp(arg.GetFileName(), "HistogramEnable") != 0)
    return false;

  arg.SetContentType("text/html; charset=utf-8");
  arg.SetContent(
      "<!doctype html><html><head><meta charset=\"utf-8\">"
      "<meta http-equiv=\"refresh\" content=\"0; url=/Analyzer/HistogramEnable/\">"
      "</head><body><a href=\"/Analyzer/HistogramEnable/\">"
      "Open HistogramEnable</a></body></html>");
  arg.AddHeader("Refresh", "0; url=/Analyzer/HistogramEnable/");
  arg.AddNoCacheHeader();
  return true;
}

void HistogramEnableHttpServer::ProcessRequest(
    std::shared_ptr<THttpCallArg> arg) {
  if (arg && RedirectHistogramEnableEntry(*arg)) return;
  THttpServer::ProcessRequest(std::move(arg));
}

}  // namespace ana
