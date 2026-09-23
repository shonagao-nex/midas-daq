#include "AnalyzerCli.h"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace ana {
namespace {

constexpr const char* kRootOutputDirectory =
    "/home/daq/midas/midas/rootfiles";

bool StartsWith(const std::string& text, const char* prefix) {
  return text.rfind(prefix, 0) == 0;
}

bool ParseLimit(const std::string& text, std::size_t* value) {
  if (text.empty()) return false;
  unsigned long long parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    return false;
  *value = static_cast<std::size_t>(parsed);
  return parsed == *value;
}

std::string DefaultOutputName(const std::string& input) {
  std::string name = std::filesystem::path(input).filename().string();
  if (name.size() >= 8 && name.compare(name.size() - 8, 8, ".mid.lz4") == 0)
    name.resize(name.size() - 8);
  else if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".mid") == 0)
    name.resize(name.size() - 4);
  return (std::filesystem::path(kRootOutputDirectory) / (name + ".root"))
      .string();
}

std::string ResolveOutputName(const std::string& requested) {
  const std::filesystem::path path(requested);
  if (path.is_absolute() || !path.parent_path().empty()) return path.string();
  return (std::filesystem::path(kRootOutputDirectory) / path).string();
}

}  // namespace

AnalyzerCliResult ParseAnalyzerCli(const std::vector<std::string>& arguments) {
  AnalyzerCliResult result;
  if (arguments.empty()) {
    result.error = "missing program name";
    return result;
  }

  result.manalyzer_arguments.push_back(arguments.front());
  std::string requested_input;
  std::string requested_output;
  std::vector<std::string> positional_inputs;
  bool has_legacy_root_output = false;
  bool has_http_port = false;
  bool after_separator = false;

  const auto insert_manalyzer_option = [&](const std::string& option) {
    const auto separator =
        std::find(result.manalyzer_arguments.begin(),
                  result.manalyzer_arguments.end(), "--");
    result.manalyzer_arguments.insert(separator, option);
  };

  const std::set<std::string> options_with_value{
      "--midas-progname", "--midas-hostname", "--midas-exptname",
      "--midas-buffer",   "--midas-sampling", "--midas-event-id",
      "--midas-trigger-mask"};

  for (std::size_t i = 1; i < arguments.size(); ++i) {
    const std::string& argument = arguments[i];
    if (after_separator) {
      result.manalyzer_arguments.push_back(argument);
      continue;
    }
    if (argument == "--") {
      after_separator = true;
      result.manalyzer_arguments.push_back(argument);
      continue;
    }
    if (argument == "-h" || argument == "--help") {
      result.show_help = true;
      result.okay = true;
      return result;
    }
    if (argument == "-f" || argument == "-w" || argument == "-n") {
      if (i + 1 >= arguments.size()) {
        result.error = argument + " requires a value";
        return result;
      }
      const std::string value = arguments[++i];
      if (argument == "-f") {
        if (!requested_input.empty()) {
          result.error = "-f may be specified only once";
          return result;
        }
        requested_input = value;
      } else if (argument == "-w") {
        if (!requested_output.empty()) {
          result.error = "-w may be specified only once";
          return result;
        }
        requested_output = value;
      } else if (!ParseLimit(value,
                             &result.inspector_options.decoded_event_limit)) {
        result.error = "-n requires a non-negative integer";
        return result;
      }
      continue;
    }
    if (argument == "--mt") {
      result.error = "--mt is not supported by this analyzer; histogram "
                     "publication and reload use the single-thread event loop";
      return result;
    }
    if (options_with_value.count(argument)) {
      if (i + 1 >= arguments.size()) {
        result.error = argument + " requires a value";
        return result;
      }
      result.manalyzer_arguments.push_back(argument);
      result.manalyzer_arguments.push_back(arguments[++i]);
      continue;
    }
    if (StartsWith(argument, "-O") || StartsWith(argument, "-D"))
      has_legacy_root_output = true;
    if (StartsWith(argument, "-R")) has_http_port = true;

    if (!argument.empty() && argument.front() != '-')
      positional_inputs.push_back(argument);
    result.manalyzer_arguments.push_back(argument);
  }

  if (!requested_input.empty() && !positional_inputs.empty()) {
    result.error = "do not combine -f with positional MIDAS input files";
    return result;
  }
  if (!requested_input.empty()) {
    result.input_file = requested_input;
    insert_manalyzer_option(requested_input);
  } else if (!positional_inputs.empty()) {
    result.input_file = positional_inputs.front();
  }

  const bool offline = !result.input_file.empty();
  result.inspector_options.mode =
      offline ? AnalyzerMode::kOffline : AnalyzerMode::kOnline;

  if (!offline) {
    if (!requested_output.empty() || has_legacy_root_output) {
      result.error = "ROOT output options (-w, -O, -D) are invalid in online "
                     "mode";
      return result;
    }
    if (!has_http_port) insert_manalyzer_option("-R8081");
  } else {
    if (!requested_output.empty() && has_legacy_root_output) {
      result.error = "do not combine -w with legacy -O/-D options";
      return result;
    }
    if (!requested_output.empty()) {
      result.output_file = ResolveOutputName(requested_output);
    } else if (!has_legacy_root_output) {
      result.output_file = DefaultOutputName(result.input_file);
    }
    if (!result.output_file.empty())
      insert_manalyzer_option("-O" + result.output_file);
  }

  result.okay = true;
  return result;
}

std::string AnalyzerHelp(const std::string& program_name) {
  std::ostringstream help;
  help << "Usage:\n"
       << "  " << program_name << " [online options]\n"
       << "  " << program_name << " -f INPUT [offline options]\n\n"
       << "Options:\n"
       << "  -f FILE    Input MIDAS file for offline analysis\n"
       << "  -w FILE    Offline ROOT output filename\n"
       << "             Default: /home/daq/midas/midas/rootfiles/<input>.root\n"
       << "  -n N       Process at most N decoded events (0 = unlimited)\n"
       << "  -h         Show this help\n\n"
       << "Online defaults:\n"
       << "  No -f selects live MIDAS mode and enables ROOT THttpServer on "
          "127.0.0.1:8081. No ROOT file is created.\n\n"
       << "Selected manalyzer options remain available:\n"
       << "  -RPORT, -Hhost, -Eexperiment, --midas-*, --no-profiler\n"
       << "  -O/-D remain legacy offline ROOT output options; -e counts raw "
          "MIDAS records, while -n counts decoded events.\n";
  return help.str();
}

}  // namespace ana
