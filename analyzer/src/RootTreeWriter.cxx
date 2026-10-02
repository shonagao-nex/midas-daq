#include "RootTreeWriter.h"

#include "TFile.h"
#include "TTree.h"

#include <cstdio>

namespace ana {

bool RootTreeWriter::BeginRun(TFile* output_file) {
  EndRun();
  if (!output_file || !output_file->IsOpen() || output_file->IsZombie()) {
    std::fprintf(stderr,
                 "ERROR: RootTreeWriter cannot use an invalid ROOT file\n");
    return false;
  }

  static_assert(sizeof(std::int32_t) == sizeof(Int_t));
  static_assert(sizeof(long long) == sizeof(Long64_t));

  output_file_ = output_file;
  output_file_->cd();
  tree_ = new TTree("tree", "Decoded events");

  tree_->Branch("event", &event_, "event/L");
  tree_->Branch("vme_counter", &vme_counter_, "vme_counter/L");
  tree_->Branch("easiroc_counter", &easiroc_counter_, "easiroc_counter/L");
  tree_->Branch("v792_counter", &v792_counter_, "v792_counter/L");
  tree_->Branch("v775_counter", &v775_counter_, "v775_counter/L");
  tree_->Branch("v1190_counter", &v1190_counter_, "v1190_counter/L");
  tree_->Branch("v1720_counter", &v1720_counter_, "v1720_counter/L");
  tree_->Branch("easiroc_raw_counter", &easiroc_raw_counter_,
                "easiroc_raw_counter/L");

  tree_->Branch("qdc0", buffer_.v792.qdc0.data(), "qdc0[32]/I");
  tree_->Branch("tdc0", buffer_.v775.tdc0.data(), "tdc0[32]/I");
  tree_->Branch("tle0", &buffer_.v1190.tle0);
  tree_->Branch("ttr0", &buffer_.v1190.ttr0);
  tree_->Branch("fadc0", &buffer_.v1720.fadc0);
  tree_->Branch("eadc0", buffer_.easiroc.eadc0.data(), "eadc0[64]/I");
  tree_->Branch("etle0", &buffer_.easiroc.etle0);
  tree_->Branch("ettr0", &buffer_.easiroc.ettr0);
  return true;
}

void RootTreeWriter::Fill(const DecodedEvent& event) {
  if (!tree_) return;
  event_ = event.counters.event;
  vme_counter_ = event.counters.vme;
  easiroc_counter_ = event.counters.easiroc;
  v792_counter_ = event.counters.v792;
  v775_counter_ = event.counters.v775;
  v1190_counter_ = event.counters.v1190;
  v1720_counter_ = event.counters.v1720;
  easiroc_raw_counter_ = event.counters.nim_easiroc;
  buffer_ = event;
  tree_->Fill();
}

void RootTreeWriter::EndRun() {
  if (!tree_) return;
  tree_ = nullptr;
  output_file_ = nullptr;
}

std::int64_t RootTreeWriter::Entries() const {
  return tree_ ? tree_->GetEntries() : 0;
}

}  // namespace ana
