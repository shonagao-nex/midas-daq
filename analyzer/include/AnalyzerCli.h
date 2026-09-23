#ifndef ANA_ANALYZER_CLI_H
#define ANA_ANALYZER_CLI_H

#include "AnalyzerMode.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ana {

struct AnalyzerCliResult {
  bool okay = false;
  bool show_help = false;
  bool init_hist_odb = false;
  EventInspectorOptions inspector_options;
  std::vector<std::string> manalyzer_arguments;
  std::string midas_program_name = "ana_hist_odb_init";
  std::string midas_hostname;
  std::string midas_experiment;
  std::string input_file;
  std::string output_file;
  std::string error;
};

AnalyzerCliResult ParseAnalyzerCli(const std::vector<std::string>& arguments);
std::string AnalyzerHelp(const std::string& program_name);

}  // namespace ana

#endif
