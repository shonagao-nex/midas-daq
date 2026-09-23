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
  EventInspectorOptions inspector_options;
  std::vector<std::string> manalyzer_arguments;
  std::string input_file;
  std::string output_file;
  std::string error;
};

AnalyzerCliResult ParseAnalyzerCli(const std::vector<std::string>& arguments);
std::string AnalyzerHelp(const std::string& program_name);

}  // namespace ana

#endif
