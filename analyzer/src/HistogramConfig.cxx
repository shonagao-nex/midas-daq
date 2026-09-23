#include "HistogramConfig.h"

#include "ExpressionResolver.h"

#include <cstdio>
#include <set>
#include <string>
#include <utility>

namespace ana {

bool operator==(const HistogramConfig& left, const HistogramConfig& right) {
  return left.hist_name == right.hist_name && left.type == right.type &&
         left.expression == right.expression && left.bins == right.bins &&
         left.min == right.min && left.max == right.max &&
         left.cut == right.cut && left.enabled == right.enabled &&
         left.title == right.title && left.x_title == right.x_title &&
         left.y_title == right.y_title && left.group == right.group &&
         left.slot == right.slot;
}

bool operator!=(const HistogramConfig& left, const HistogramConfig& right) {
  return !(left == right);
}

std::vector<HistogramConfig> DefaultHistogramConfigs() {
  std::vector<HistogramConfig> configs;
  configs.reserve(521);

  const auto add = [&configs](std::string group, int channel_count,
                              const char* expression_format,
                              const char* name_prefix, const char* title_prefix,
                              const char* x_title, int bins, double min,
                              double max, bool event_group = false) {
    for (int channel = 0; channel < channel_count; ++channel) {
      char slot[16];
      std::snprintf(slot, sizeof(slot), "Ch%02d", channel);
      char expression[64];
      std::snprintf(expression, sizeof(expression), expression_format,
                    channel);
      char hist_name[96];
      if (event_group)
        std::snprintf(hist_name, sizeof(hist_name), "h_event");
      else
        std::snprintf(hist_name, sizeof(hist_name), "%s_ch%02d", name_prefix,
                      channel);
      char title[128];
      if (event_group)
        std::snprintf(title, sizeof(title), "Event");
      else
        std::snprintf(title, sizeof(title), "%s Ch.%d", title_prefix,
                      channel);
      configs.push_back({hist_name, "TH1D", expression, bins, min, max, "",
                         event_group, title, x_title, "Counts", group, slot});
    }
  };

  add("Event", 1, "event", "", "", "Event number", 400, 0.0, 400.0,
      true);
  add("QDC0", 32, "qdc0[%d]", "h_qdc0", "QDC0", "QDC raw value", 4096,
      0.0, 4096.0);
  add("TDC0", 32, "tdc0[%d]", "h_tdc0", "TDC0", "TDC raw value", 4096,
      0.0, 4096.0);
  add("TLE0", 128, "tle0[%d][0]", "h_tle0", "V1190 Leading",
      "Leading time raw value", 4096, 0.0, 1048576.0);
  add("TTR0", 128, "ttr0[%d][0]", "h_ttr0", "V1190 Trailing",
      "Trailing time raw value", 4096, 0.0, 1048576.0);
  add("EADC0", 64, "eadc0[%d]", "h_eadc0", "EASIROC ADC", "ADC raw value",
      4096, 0.0, 4096.0);
  add("ETLE0", 64, "etle0[%d][0]", "h_etle0", "EASIROC Leading",
      "Leading time raw value", 4096, 0.0, 1048576.0);
  add("ETTR0", 64, "ettr0[%d][0]", "h_ettr0", "EASIROC Trailing",
      "Trailing time raw value", 4096, 0.0, 1048576.0);
  add("FADC0", 8, "fadc0[%d][0]", "h_fadc0", "FADC0", "FADC raw value",
      4096, 0.0, 4096.0);
  return configs;
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
