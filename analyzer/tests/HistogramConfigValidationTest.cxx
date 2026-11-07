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

  auto waves = ana::DefaultWaveformConfigs();
  okay &= Check(ana::ValidateHistogramConfigs(waves).skipped == 0,
                "eight waveform defaults validate");
  const std::vector<std::string> bad_wave_expressions{
      "fadc0[8]", "qdc0[0]", "fadc0[0][0]", "fadc0[-1]", "fadc0[0]junk"};
  for (const auto& text : bad_wave_expressions) {
    auto bad = waves.front();
    bad.expression = text;
    okay &= Check(ana::ValidateHistogramConfigs({bad}).skipped == 1,
                  "waveform rejects invalid channel or scalar expression");
  }
  auto bad_wave = waves.front();
  bad_wave.bins = 128;
  okay &= Check(ana::ValidateHistogramConfigs({bad_wave}).skipped == 1,
                "waveform rejects rebinning that would merge samples");
  bad_wave = waves.front();
  bad_wave.min = 0;
  bad_wave.max = 256;
  okay &= Check(ana::ValidateHistogramConfigs({bad_wave}).skipped == 1,
                "waveform requires sample-centered bins");
  auto scalar_wave = waves.front();
  scalar_wave.type = "TH1D";
  okay &= Check(ana::ValidateHistogramConfigs({scalar_wave}).skipped == 1,
                "ordinary TH1D cannot take a whole waveform expression");

  if (!okay) return 1;
  std::printf("HistogramConfig validation tests passed\n");
  return 0;
}
