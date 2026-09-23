#include "HistogramEnableHttpServer.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main() {
  THttpCallArg bare;
  bare.SetMethod("GET");
  bare.SetPathAndFileName("/Analyzer/HistogramEnable");
  if (!ana::RedirectHistogramEnableEntry(bare)) return 1;
  const std::string reply = bare.FillHttpHeader() +
      std::string(static_cast<const char*>(bare.GetContent()),
                  static_cast<std::size_t>(bare.GetContentLength()));
  if (reply.find("Refresh: 0; url=/Analyzer/HistogramEnable/") ==
          std::string::npos ||
      reply.find("href=\"/Analyzer/HistogramEnable/\"") ==
          std::string::npos)
    return 2;

  THttpCallArg directory;
  directory.SetMethod("GET");
  directory.SetPathAndFileName("/Analyzer/HistogramEnable/");
  if (ana::RedirectHistogramEnableEntry(directory)) return 3;

  THttpCallArg unrelated;
  unrelated.SetMethod("GET");
  unrelated.SetPathAndFileName("/Analyzer/Other");
  if (ana::RedirectHistogramEnableEntry(unrelated)) return 4;

  THttpCallArg post;
  post.SetMethod("POST");
  post.SetPathAndFileName("/Analyzer/HistogramEnable");
  if (ana::RedirectHistogramEnableEntry(post)) return 5;

  std::ifstream home_file(ANA_ROOT_WEB_HOME);
  if (!home_file) return 6;
  const std::string home((std::istreambuf_iterator<char>(home_file)),
                         std::istreambuf_iterator<char>());
  if (home.find("<a href=\"/Analyzer/HistogramEnable/\">Histogram Enable</a>") ==
          std::string::npos ||
      home.find("<!--jsroot_importmap-->") == std::string::npos ||
      home.find("\"$$$h.json$$$\"") == std::string::npos ||
      home.find("buildGUI('onlineGUI', 'online')") == std::string::npos)
    return 7;

  std::puts("HistogramEnable homepage link and URL redirect checks passed");
  return 0;
}
