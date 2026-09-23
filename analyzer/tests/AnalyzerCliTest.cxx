#include "AnalyzerCli.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message.c_str());
  return false;
}

bool Contains(const std::vector<std::string>& values,
              const std::string& expected) {
  for (const auto& value : values)
    if (value == expected) return true;
  return false;
}

}  // namespace

int main() {
  bool okay = true;

  const auto online = ana::ParseAnalyzerCli({"midas_analyzer"});
  okay &= Check(online.okay &&
                    online.inspector_options.mode == ana::AnalyzerMode::kOnline,
                "no -f should select online mode");
  okay &= Check(Contains(online.manalyzer_arguments, "-R8081"),
                "online mode should enable the standard ROOT web server");

  const auto offline = ana::ParseAnalyzerCli(
      {"midas_analyzer", "-f", "/data/run00062.mid.lz4", "-n", "100"});
  okay &= Check(offline.okay && offline.inspector_options.mode ==
                                      ana::AnalyzerMode::kOffline,
                "-f should select offline mode");
  okay &= Check(offline.output_file ==
                    "/home/daq/midas/midas/rootfiles/run00062.root",
                "offline default filename should strip .mid.lz4");
  okay &= Check(offline.inspector_options.decoded_event_limit == 100,
                "-n should set the decoded event limit");

  const auto with_module_args = ana::ParseAnalyzerCli(
      {"midas_analyzer", "-f", "run.mid", "--", "module-option"});
  const auto& translated = with_module_args.manalyzer_arguments;
  std::size_t separator = translated.size();
  std::size_t input = translated.size();
  for (std::size_t i = 0; i < translated.size(); ++i) {
    if (translated[i] == "--") separator = i;
    if (translated[i] == "run.mid") input = i;
  }
  okay &= Check(input < separator,
                "translated input must precede manalyzer module separator");

  const auto relative = ana::ParseAnalyzerCli(
      {"midas_analyzer", "-f", "run.mid", "-w", "test.root"});
  okay &= Check(relative.output_file ==
                    "/home/daq/midas/midas/rootfiles/test.root",
                "relative basename output should use rootfiles");

  const auto absolute = ana::ParseAnalyzerCli(
      {"midas_analyzer", "-f", "run.mid", "-w", "/tmp/test.root"});
  okay &= Check(absolute.output_file == "/tmp/test.root",
                "absolute output should remain unchanged");

  const auto invalid_online =
      ana::ParseAnalyzerCli({"midas_analyzer", "-w", "bad.root"});
  okay &= Check(!invalid_online.okay,
                "-w without -f should fail in online mode");

  const auto multithread =
      ana::ParseAnalyzerCli({"midas_analyzer", "--mt"});
  okay &= Check(!multithread.okay, "--mt should be rejected");

  if (!okay) return 1;
  std::printf("Analyzer CLI tests passed\n");
  return 0;
}
