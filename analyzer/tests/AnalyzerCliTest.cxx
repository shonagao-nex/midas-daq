#include "AnalyzerCli.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
  const char* home = std::getenv("HOME");
  const auto rootfiles = std::filesystem::path(home ? home : "") /
                         "midas/midas/rootfiles";

  const auto online = ana::ParseAnalyzerCli({"midas_analyzer"});
  okay &= Check(online.okay &&
                    online.inspector_options.mode == ana::AnalyzerMode::kOnline,
                "no -f should select online mode");
  okay &= Check(online.root_web_port == 8082 &&
                    !Contains(online.manalyzer_arguments, "-R8082"),
                "online mode should own the wildcard-bound ROOT web server");

  const auto explicit_port =
      ana::ParseAnalyzerCli({"midas_analyzer", "-R9090"});
  okay &= Check(explicit_port.okay &&
                    explicit_port.root_web_port == 9090 &&
                    !Contains(explicit_port.manalyzer_arguments, "-R9090"),
                "an explicit ROOT web port should be preserved");
  okay &= Check(explicit_port.root_web_port != 8082,
                "the default port must not override an explicit ROOT web port");

  const auto normal_online =
      ana::ParseAnalyzerCli({"midas_analyzer", "--no-profiler"});
  okay &= Check(normal_online.okay && !normal_online.init_hist_odb &&
                    !normal_online.inspector_options.debug_events &&
                    Contains(normal_online.manalyzer_arguments,
                             "--no-profiler"),
                "normal online startup must remain separate from init mode");

  const auto debug_events =
      ana::ParseAnalyzerCli({"midas_analyzer", "--debug-events"});
  okay &= Check(debug_events.okay &&
                    debug_events.inspector_options.debug_events &&
                    !Contains(debug_events.manalyzer_arguments, "--debug-events"),
                "debug event output must be explicit and analyzer-owned");

  const auto initialize =
      ana::ParseAnalyzerCli({"midas_analyzer", "--init-hist-odb"});
  okay &= Check(initialize.okay && initialize.init_hist_odb,
                "--init-hist-odb should select management mode");
  okay &= Check(!Contains(initialize.manalyzer_arguments, "-R8082"),
                "management mode must not start the ROOT web server");
  okay &= Check(initialize.root_web_port == 0,
                "management mode should not configure a ROOT web port");
  okay &= Check(!ana::ParseAnalyzerCli(
                     {"midas_analyzer", "--init-hist-odb", "--debug-events"})
                     .okay,
                "management mode must reject event debugging");

  const auto initialize_pages =
      ana::ParseAnalyzerCli({"midas_analyzer", "--init-page-odb"});
  okay &= Check(initialize_pages.okay && initialize_pages.init_page_odb &&
                    !initialize_pages.init_hist_odb &&
                    initialize_pages.root_web_port == 0,
                "page initialization must be a separate management mode");
  okay &= Check(!ana::ParseAnalyzerCli(
                     {"midas_analyzer", "--init-page-odb", "--init-hist-odb"})
                     .okay &&
                    !ana::ParseAnalyzerCli(
                         {"midas_analyzer", "--init-page-odb", "-f", "run.mid"})
                         .okay,
                "page initialization must reject other modes");

  const auto initialize_with_connection = ana::ParseAnalyzerCli(
      {"midas_analyzer", "--init-hist-odb", "-Hdaqhost", "-Edaq",
       "--midas-progname", "hist_init"});
  okay &= Check(initialize_with_connection.okay &&
                    initialize_with_connection.midas_hostname == "daqhost" &&
                    initialize_with_connection.midas_experiment == "daq" &&
                    initialize_with_connection.midas_program_name ==
                        "hist_init",
                "management mode should retain MIDAS connection options");

  const auto initialize_with_input = ana::ParseAnalyzerCli(
      {"midas_analyzer", "--init-hist-odb", "-f", "run.mid"});
  okay &= Check(!initialize_with_input.okay,
                "management mode must reject offline input");
  const auto initialize_with_output = ana::ParseAnalyzerCli(
      {"midas_analyzer", "--init-hist-odb", "-w", "run.root"});
  okay &= Check(!initialize_with_output.okay,
                "management mode must reject offline output");
  const auto initialize_with_limit = ana::ParseAnalyzerCli(
      {"midas_analyzer", "--init-hist-odb", "-n", "1"});
  okay &= Check(!initialize_with_limit.okay,
                "management mode must reject event limits");
  const auto initialize_with_legacy_output = ana::ParseAnalyzerCli(
      {"midas_analyzer", "--init-hist-odb", "-Olegacy.root"});
  okay &= Check(!initialize_with_legacy_output.okay,
                "management mode must reject legacy ROOT output");
  const auto initialize_with_web = ana::ParseAnalyzerCli(
      {"midas_analyzer", "--init-hist-odb", "-R9090"});
  okay &= Check(!initialize_with_web.okay,
                "management mode must reject ROOT web server options");

  const auto offline = ana::ParseAnalyzerCli(
      {"midas_analyzer", "-f", "/data/run00062.mid.lz4", "-n", "100"});
  okay &= Check(offline.okay && offline.inspector_options.mode ==
                                      ana::AnalyzerMode::kOffline &&
                    offline.root_web_port == 0,
                "-f should select offline mode");
  okay &= Check(!ana::ParseAnalyzerCli(
                     {"midas_analyzer", "-f", "run.mid", "-R9090"}).okay,
                "offline mode must not start a ROOT web server");
  okay &= Check(!ana::ParseAnalyzerCli({"midas_analyzer", "-R0"}).okay &&
                    !ana::ParseAnalyzerCli({"midas_analyzer", "-R65536"}).okay,
                "invalid ROOT web ports should be rejected");
  okay &= Check(offline.output_file ==
                    (rootfiles / "run00062.root").string(),
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
                    (rootfiles / "test.root").string(),
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
