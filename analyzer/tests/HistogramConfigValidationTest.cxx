#include "HistogramConfig.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

bool Check(bool condition, const std::string& message) {
  if (condition) return true;
  std::fprintf(stderr, "FAIL: %s\n", message.c_str());
  return false;
}

ana::HistogramConfig Valid(std::string name) {
  return {std::move(name), "TH1D", "qdc0[0]", 4096, 0.0, 4096.0, "",
          true, "", "", "", "", ""};
}

}  // namespace

int main() {
  std::vector<ana::HistogramConfig> configs;
  configs.push_back(Valid("valid"));

  auto bins = Valid("bins");
  bins.bins = 0;
  configs.push_back(bins);

  auto range = Valid("range");
  range.min = range.max;
  configs.push_back(range);

  auto type = Valid("type");
  type.type = "TH2D";
  configs.push_back(type);

  auto expression = Valid("expression");
  expression.expression = "qdc0[32]";
  configs.push_back(expression);

  auto cut = Valid("cut");
  cut.cut = "qdc0[0]>100";
  configs.push_back(cut);

  configs.push_back(Valid(""));
  configs.push_back(Valid("valid"));

  auto disabled = Valid("disabled");
  disabled.enabled = false;
  configs.push_back(disabled);

  const auto result = ana::ValidateHistogramConfigs(configs);
  bool okay = true;
  okay &= Check(result.configs.size() == 2,
                "only valid and disabled-valid configs should remain");
  okay &= Check(result.skipped == 7, "seven invalid/duplicate configs skipped");
  okay &= Check(result.configs[0].hist_name == "valid",
                "first duplicate definition should win");
  okay &= Check(result.configs[1].hist_name == "disabled" &&
                    !result.configs[1].enabled,
                "disabled valid config should be retained");

  if (!okay) return 1;
  std::printf("HistogramConfig validation tests passed\n");
  return 0;
}
