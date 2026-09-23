#ifndef ANA_HISTOGRAM_PDF_WRITER_H
#define ANA_HISTOGRAM_PDF_WRITER_H

#include <string>

namespace ana {

class HistogramManager;

class HistogramPdfWriter {
 public:
  bool Write(const HistogramManager& manager, int run_number,
             const std::string& output_path, std::string* error = nullptr) const;
};

}  // namespace ana

#endif
