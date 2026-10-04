#include "AnalyzerCli.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace ana {
namespace {

std::filesystem::path RootOutputDirectory() {
  const char* home = std::getenv("HOME");
  return std::filesystem::path(home ? home : "") / "midas/midas/rootfiles";
}

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

bool ParseWebPort(const std::string& text, int* port) {
  if (text.empty()) return false;
  int parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed < 1 || parsed > 65535)
    return false;
  *port = parsed;
  return true;
}

std::string DefaultOutputName(const std::string& input) {
  std::string name = std::filesystem::path(input).filename().string();
  if (name.size() >= 8 && name.compare(name.size() - 8, 8, ".mid.lz4") == 0)
    name.resize(name.size() - 8);
  else if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".mid") == 0)
    name.resize(name.size() - 4);
  return (RootOutputDirectory() / (name + ".root"))
      .string();
}

std::string ResolveOutputName(const std::string& requested) {
  const std::filesystem::path path(requested);
  if (path.is_absolute() || !path.parent_path().empty()) return path.string();
  return (RootOutputDirectory() / path).string();
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
  bool has_limit = false;
  std::string management_incompatible_option;
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
      if (management_incompatible_option.empty())
        management_incompatible_option = "module arguments after --";
      result.manalyzer_arguments.push_back(argument);
      continue;
    }
    if (argument == "--") {
      after_separator = true;
      if (management_incompatible_option.empty())
        management_incompatible_option = "--";
      result.manalyzer_arguments.push_back(argument);
      continue;
    }
    if (argument == "-h" || argument == "--help") {
      result.show_help = true;
      result.okay = true;
      return result;
    }
    if (argument == "--init-hist-odb") {
      result.init_hist_odb = true;
      continue;
    }
    if (argument == "--init-page-odb") {
      result.init_page_odb = true;
      continue;
    }
    if (argument == "--debug-events") {
      result.inspector_options.debug_events = true;
      continue;
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
      } else {
        has_limit = true;
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
      const std::string value = arguments[++i];
      if (argument == "--midas-progname")
        result.midas_program_name = value;
      else if (argument == "--midas-hostname")
        result.midas_hostname = value;
      else if (argument == "--midas-exptname")
        result.midas_experiment = value;
      else if (management_incompatible_option.empty())
        management_incompatible_option = argument;
      result.manalyzer_arguments.push_back(argument);
      result.manalyzer_arguments.push_back(value);
      continue;
    }
    if (StartsWith(argument, "-O") || StartsWith(argument, "-D"))
      has_legacy_root_output = true;
    if (StartsWith(argument, "-R")) {
      if (!ParseWebPort(argument.substr(2), &result.root_web_port)) {
        result.error = "-R requires a TCP port from 1 to 65535";
        return result;
      }
      has_http_port = true;
      // This analyzer owns the ROOT server so it can bind to all interfaces.
      // Passing -R to manalyzer would start its hard-coded loopback listener.
      continue;
    }
    if (StartsWith(argument, "-H"))
      result.midas_hostname = argument.substr(2);
    else if (StartsWith(argument, "-E"))
      result.midas_experiment = argument.substr(2);
    else if (!StartsWith(argument, "-O") && !StartsWith(argument, "-D") &&
             management_incompatible_option.empty())
      management_incompatible_option = argument;

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

  if (result.init_hist_odb || result.init_page_odb) {
    if ((result.init_hist_odb && result.init_page_odb) ||
        !requested_input.empty() || !positional_inputs.empty() ||
        !requested_output.empty() || has_limit || has_legacy_root_output ||
        has_http_port || result.inspector_options.debug_events ||
        !management_incompatible_option.empty()) {
      result.error = "ODB initialization cannot be combined with analysis, "
                     "event-loop, or ROOT output options";
      return result;
    }
    result.okay = true;
    return result;
  }

  if (!offline) {
    if (!requested_output.empty() || has_legacy_root_output) {
      result.error = "ROOT output options (-w, -O, -D) are invalid in online "
                     "mode";
      return result;
    }
    if (!has_http_port) result.root_web_port = 8082;
  } else {
    if (has_http_port) {
      result.error = "-R is available only in online mode";
      return result;
    }
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
       << "  " << program_name << " -f INPUT [offline options]\n"
       << "  " << program_name << " --init-hist-odb [connection options]\n\n"
       << "  " << program_name << " --init-page-odb [connection options]\n\n"
       << "Options:\n"
       << "  -f FILE    Input MIDAS file for offline analysis\n"
       << "  -w FILE    Offline ROOT output filename\n"
       << "             Default: " << RootOutputDirectory().string()
       << "/<input>.root\n"
       << "  -n N       Process at most N decoded events (0 = unlimited)\n"
       << "  --debug-events  Print details for the first 8 events of each run\n"
       << "  -h         Show this help\n\n"
       << "Management option:\n"
       << "  --init-hist-odb\n"
       << "      Create default histogram configuration in MIDAS ODB and exit.\n"
       << "      Existing configuration is never overwritten.\n\n"
       << "  --init-page-odb\n"
       << "      Create four default Online Pages in MIDAS ODB and exit.\n"
       << "      Existing /Analyzer/Pages is never overwritten.\n\n"
       << "Online defaults:\n"
       << "  No -f selects live MIDAS mode and enables ROOT THttpServer on "
          "0.0.0.0:8082. No ROOT file is created.\n"
       << "  -RPORT overrides 8082 and binds to all interfaces.\n\n"
       << "Selected manalyzer options remain available:\n"
       << "  -RPORT, -Hhost, -Eexperiment, --midas-*, --no-profiler\n"
       << "  -O/-D remain legacy offline ROOT output options; -e counts raw "
          "MIDAS records, while -n counts decoded events.\n";
  return help.str();
}

}  // namespace ana
