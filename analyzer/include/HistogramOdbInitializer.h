#ifndef ANA_HISTOGRAM_ODB_INITIALIZER_H
#define ANA_HISTOGRAM_ODB_INITIALIZER_H

#include <cstddef>
#include <string>
#include <vector>

class MVOdb;

namespace ana {

struct HistogramOdbInitializationResult {
  bool okay = false;
  std::size_t created = 0;
  std::size_t loaded = 0;
  std::size_t valid = 0;
  std::vector<std::string> histogram_names;
  std::string error;
};

HistogramOdbInitializationResult InitializeHistogramOdb(MVOdb* odb);
// Requires existing Histograms and Pages; never overwrites either target.
bool InitializeWaveformOdb(MVOdb* odb, std::string* error);

}  // namespace ana

#endif
