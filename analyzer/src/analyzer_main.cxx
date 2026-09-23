#include "AnalyzerCli.h"
#include "EventInspector.h"
#include "HistogramConfigLoader.h"
#include "HistogramOdbInitializer.h"
#include "HistogramEnableWebHandler.h"
#include "HistogramEnableHttpServer.h"
#include "OnlineHistogramState.h"
#include "manalyzer.h"
#include "midas.h"
#include "mvodb.h"
#include "tmfe.h"
#include "THttpServer.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

int InitializeHistogramOdb(const ana::AnalyzerCliResult& options) {
  const char* hostname =
      options.midas_hostname.empty() ? nullptr : options.midas_hostname.c_str();
  const char* experiment = options.midas_experiment.empty()
                               ? nullptr
                               : options.midas_experiment.c_str();
  int status = cm_connect_experiment(hostname, experiment,
                                     options.midas_program_name.c_str(),
                                     nullptr);
  if (status != CM_SUCCESS) {
    std::fprintf(stderr, "ERROR: cannot connect to MIDAS: %s (status %d)\n",
                 cm_get_error(status).c_str(), status);
    return 1;
  }

  HNDLE database = 0;
  status = cm_get_experiment_database(&database, nullptr);
  if (status != CM_SUCCESS) {
    std::fprintf(stderr,
                 "ERROR: cannot obtain MIDAS ODB handle: %s (status %d)\n",
                 cm_get_error(status).c_str(), status);
    cm_disconnect_experiment();
    return 1;
  }

  std::unique_ptr<MVOdb> odb(MakeMidasOdb(database));
  const auto result = ana::InitializeHistogramOdb(odb.get());
  odb.reset();
  status = cm_disconnect_experiment();
  if (!result.okay) {
    std::fprintf(stderr, "ERROR: histogram ODB initialization failed: %s\n",
                 result.error.c_str());
    return 1;
  }
  if (status != CM_SUCCESS) {
    std::fprintf(stderr, "ERROR: MIDAS disconnect failed: %s (status %d)\n",
                 cm_get_error(status).c_str(), status);
    return 1;
  }

  std::printf("Created histogram ODB defaults under:\n  %s\n\nHistograms:\n",
              ana::HistogramConfigLoader::kOdbPath);
  for (const auto& name : result.histogram_names)
    std::printf("  %s\n", name.c_str());
  std::printf("\n%zu configs created\n%zu configs loaded\n"
              "%zu configs valid\n\nODB initialization completed.\n",
              result.created, result.loaded, result.valid);
  return 0;
}

class EventInspectorFactory : public TAFactory {
 public:
  void SetOptions(ana::EventInspectorOptions options, int root_web_port) {
    options_ = options;
    root_web_port_ = root_web_port;
  }

  void Init(const std::vector<std::string>&) override {
    if (options_.mode != ana::AnalyzerMode::kOnline) return;

    if (!TARootHelper::fgHttpServer && root_web_port_ > 0) {
      const std::string address =
          "http:0.0.0.0:" + std::to_string(root_web_port_);
      auto* server = new ana::HistogramEnableHttpServer(address.c_str());
      if (!server->IsAnyEngine()) {
        std::fprintf(stderr,
                     "ERROR: cannot start ROOT Web on 0.0.0.0:%d\n",
                     root_web_port_);
        delete server;
        std::exit(EXIT_FAILURE);
      }
      server->SetDefaultPage(ANA_ROOT_WEB_HOME);
      TARootHelper::fgHttpServer = server;
    }

    // manalyzer calls module Init() after TMFE has connected, including while
    // the run is stopped (when no TARunObject exists). This makes the initial
    // histogram configuration check observable without starting a run.
    TMFE* mfe = TMFE::Instance();
    if (!pdf_watch_active_ && mfe && mfe->fOdbRoot) {
      HNDLE database = 0;
      HNDLE analyzer_key = 0;
      if (cm_get_experiment_database(&database, nullptr) == CM_SUCCESS &&
          db_find_key(database, 0, "/Analyzer", &analyzer_key) == DB_SUCCESS &&
          analyzer_key != 0 &&
          db_watch(database, analyzer_key, &EventInspectorFactory::PdfOdbChanged,
                   this) == DB_SUCCESS) {
        pdf_watch_database_ = database;
        pdf_watch_key_ = analyzer_key;
        pdf_watch_active_ = true;
      } else {
        std::fprintf(stderr,
                     "INFO: /Analyzer ODB tree is absent; STOP-state PDF "
                     "request watch is unavailable until analyzer restart.\n");
      }
    }
    if (!histogram_enable_web_ && TARootHelper::fgHttpServer && mfe &&
        mfe->fOdbRoot) {
      histogram_enable_web_ =
          std::make_unique<ana::HistogramEnableWebHandler>(mfe->fOdbRoot);
      if (!TARootHelper::fgHttpServer->Register(
              "/Analyzer", histogram_enable_web_.get())) {
        std::fprintf(stderr,
                     "WARNING: cannot register histogram channel web page\n");
        histogram_enable_web_.reset();
      }
    }
    const auto result = histogram_config_loader_.Load(
        mfe ? mfe->fOdbRoot : nullptr);
    if (!result.odb_path_found) {
      std::fprintf(stderr,
                   "WARNING: %s not found; using in-memory default histogram "
                   "configuration; ODB was not modified.\n",
                   ana::HistogramConfigLoader::kOdbPath);
    } else {
      std::printf("HistogramConfigLoader: startup read-only check loaded %zu "
                  "ODB definition(s)\n",
                  result.configs.size());
    }
  }

  TARunObject* NewRunObject(TARunInfo* runinfo) override {
    return new ana::EventInspector(runinfo, options_, &online_state_);
  }

  void Finish() override {
    if (pdf_watch_active_) {
      db_unwatch(pdf_watch_database_, pdf_watch_key_);
      pdf_watch_active_ = false;
    }
    online_state_.Clear();
  }

 private:
  static void PdfOdbChanged(INT, INT, INT, void* context) {
    auto* factory = static_cast<EventInspectorFactory*>(context);
    TMFE* mfe = TMFE::Instance();
    if (factory && mfe)
      factory->online_state_.PollPdfRequest(mfe->fOdbRoot);
  }

  ana::EventInspectorOptions options_;
  int root_web_port_ = 0;
  ana::HistogramConfigLoader histogram_config_loader_;
  std::unique_ptr<ana::HistogramEnableWebHandler> histogram_enable_web_;
  ana::OnlineHistogramState online_state_;
  HNDLE pdf_watch_database_ = 0;
  HNDLE pdf_watch_key_ = 0;
  bool pdf_watch_active_ = false;
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

  if (parsed.init_hist_odb) return InitializeHistogramOdb(parsed);

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
  event_inspector_factory.SetOptions(parsed.inspector_options,
                                     parsed.root_web_port);

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
