#include "HistogramConfig.h"

#include "ExpressionResolver.h"

#include <cstdio>
#include <set>
#include <string>

namespace ana {

bool operator==(const HistogramConfig& left, const HistogramConfig& right) {
  return left.hist_name == right.hist_name && left.type == right.type &&
         left.expression == right.expression && left.bins == right.bins &&
         left.min == right.min && left.max == right.max &&
         left.cut == right.cut && left.enabled == right.enabled;
}

bool operator!=(const HistogramConfig& left, const HistogramConfig& right) {
  return !(left == right);
}

std::vector<HistogramConfig> DefaultHistogramConfigs() {
  return {
      {"h_qdc0_ch0", "TH1D", "qdc0[0]", 4096, 0.0, 4096.0, "", true},
      {"h_eadc0_ch0", "TH1D", "eadc0[0]", 4096, 0.0, 4096.0, "", true},
      {"h_event", "TH1D", "event", 400, 0.0, 400.0, "", true},
      {"h_tle0_ch0_hit0", "TH1D", "tle0[0][0]", 4096, 0.0, 1048576.0,
       "", true},
      {"h_fadc0_ch0_sample0", "TH1D", "fadc0[0][0]", 4096, 0.0,
       4096.0, "", true},
  };
}

HistogramConfigValidationResult ValidateHistogramConfigs(
    const std::vector<HistogramConfig>& configs) {
  HistogramConfigValidationResult result;
  std::set<std::string> names;
  ExpressionResolver resolver;

  for (const auto& config : configs) {
    const std::string display_name =
        config.hist_name.empty() ? "<empty>" : config.hist_name;

    if (!config.hist_name.empty() && !names.insert(config.hist_name).second) {
      std::fprintf(stderr,
                   "WARNING: histogram \"%s\" skipped: duplicate HistName "
                   "(first definition wins)\n",
                   config.hist_name.c_str());
      ++result.skipped;
      continue;
    }
    if (config.hist_name.empty()) {
      std::fprintf(stderr,
                   "WARNING: histogram \"%s\" skipped: HistName is empty\n",
                   display_name.c_str());
      ++result.skipped;
      continue;
    }
    if (config.type != "TH1D") {
      std::fprintf(stderr,
                   "WARNING: histogram \"%s\" skipped: unsupported Type "
                   "\"%s\"\n",
                   config.hist_name.c_str(), config.type.c_str());
      ++result.skipped;
      continue;
    }
    if (!config.cut.empty()) {
      std::fprintf(stderr,
                   "WARNING: histogram \"%s\" skipped: unsupported Cut "
                   "\"%s\"\n",
                   config.hist_name.c_str(), config.cut.c_str());
      ++result.skipped;
      continue;
    }
    if (config.bins <= 0) {
      std::fprintf(stderr,
                   "WARNING: histogram \"%s\" skipped: Bins must be > 0\n",
                   config.hist_name.c_str());
      ++result.skipped;
      continue;
    }
    if (!(config.min < config.max)) {
      std::fprintf(stderr,
                   "WARNING: histogram \"%s\" skipped: Min must be less "
                   "than Max\n",
                   config.hist_name.c_str());
      ++result.skipped;
      continue;
    }

    const ParsedExpression expression = resolver.Parse(config.expression);
    if (!expression.IsValid()) {
      std::fprintf(stderr,
                   "WARNING: histogram \"%s\" skipped: invalid Expression "
                   "\"%s\" (%s)\n",
                   config.hist_name.c_str(), config.expression.c_str(),
                   expression.error.c_str());
      ++result.skipped;
      continue;
    }

    result.configs.push_back(config);
  }

  return result;
}

}  // namespace ana
