#include "AnalyzerCli.h"
#include "EventInspector.h"
#include "manalyzer.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

class EventInspectorFactory : public TAFactory {
 public:
  void SetOptions(ana::EventInspectorOptions options) { options_ = options; }

  TARunObject* NewRunObject(TARunInfo* runinfo) override {
    return new ana::EventInspector(runinfo, options_);
  }

 private:
  ana::EventInspectorOptions options_;
};

EventInspectorFactory event_inspector_factory;
TARegister register_event_inspector(&event_inspector_factory);

}  // namespace

int main(int argc, char* argv[]) {
  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);

  const auto parsed = ana::ParseAnalyzerCli(arguments);
  if (parsed.show_help) {
    std::printf("%s", ana::AnalyzerHelp(arguments.front()).c_str());
    return 0;
  }
  if (!parsed.okay) {
    std::fprintf(stderr, "ERROR: %s\n\n%s", parsed.error.c_str(),
                 ana::AnalyzerHelp(arguments.front()).c_str());
    return 2;
  }

  if (parsed.inspector_options.mode == ana::AnalyzerMode::kOffline &&
      !parsed.output_file.empty()) {
    std::error_code error;
    std::filesystem::create_directories(
        std::filesystem::path(parsed.output_file).parent_path(), error);
    if (error) {
      std::fprintf(stderr, "ERROR: cannot create ROOT output directory: %s\n",
                   error.message().c_str());
      return 2;
    }
  }

  if (parsed.inspector_options.mode == ana::AnalyzerMode::kOnline)
    TARootHelper::fgUserOutputDirectory.clear();
  event_inspector_factory.SetOptions(parsed.inspector_options);

  std::vector<std::vector<char>> storage;
  std::vector<char*> translated;
  storage.reserve(parsed.manalyzer_arguments.size());
  translated.reserve(parsed.manalyzer_arguments.size());
  for (const auto& argument : parsed.manalyzer_arguments) {
    storage.emplace_back(argument.begin(), argument.end());
    storage.back().push_back('\0');
  }
  for (auto& argument : storage) translated.push_back(argument.data());
  return manalyzer_main(static_cast<int>(translated.size()), translated.data());
}
