#include "PageConfigLoader.h"

#include "mvodb.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace ana {
namespace {

using OdbPtr = std::unique_ptr<MVOdb>;

OdbPtr FindDirectory(MVOdb* odb) {
  if (!odb) return {};
  const bool print_errors = odb->GetPrintError();
  if (!odb->IsReadOnly()) odb->SetPrintError(false);
  MVOdbError error;
  OdbPtr result(odb->Chdir("Analyzer/Pages", false, &error));
  if (!odb->IsReadOnly()) odb->SetPrintError(print_errors);
  return result;
}

bool ReadInt(MVOdb* directory, const char* field, int* value) {
  MVOdbError error;
  directory->RI(field, value, false, &error);
  return !error.fError;
}

bool ReadString(MVOdb* directory, const std::string& field,
                std::string* value) {
  MVOdbError error;
  directory->RS(field.c_str(), value, false, 0, &error);
  return !error.fError;
}

}  // namespace

PageConfigLoader::Result PageConfigLoader::Load(MVOdb* odb) const {
  Result result;
  OdbPtr directory = FindDirectory(odb);
  if (!directory) return result;
  result.odb_path_found = true;
  if (!directory->IsReadOnly()) directory->SetPrintError(false);

  std::vector<std::string> names;
  MVOdbError error;
  directory->ReadDir(&names, nullptr, nullptr, nullptr, nullptr, &error);
  if (error.fError) return result;

  for (const auto& name : names) {
    OdbPtr page_directory(directory->Chdir(name.c_str(), false));
    if (!page_directory) continue;
    if (!page_directory->IsReadOnly()) page_directory->SetPrintError(false);
    PageConfig page;
    page.name = name;
    if (!ReadInt(page_directory.get(), "Rows", &page.rows) ||
        !ReadInt(page_directory.get(), "Columns", &page.columns)) {
      std::fprintf(stderr, "WARNING: page %s has incomplete layout\n",
                   name.c_str());
      continue;
    }
    if (!IsSupportedPageLayout(page)) {
      std::fprintf(stderr,
                   "WARNING: page %s has unsupported layout %dx%d; skipped\n",
                   name.c_str(), page.rows, page.columns);
      continue;
    }
    page.pads.resize(static_cast<std::size_t>(page.rows * page.columns));
    for (std::size_t index = 0; index < page.pads.size(); ++index) {
      char field[16];
      std::snprintf(field, sizeof(field), "Pad%02zu", index + 1);
      // Missing pad fields intentionally mean an empty pad.
      ReadString(page_directory.get(), field, &page.pads[index]);
    }
    result.pages.push_back(std::move(page));
  }
  result.loaded_from_odb = true;
  return result;
}

}  // namespace ana
