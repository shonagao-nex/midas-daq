#include "easiroc_daq_control.h"
#include "easiroc_event.h"
#include "easiroc_stream.h"
#include "rbcp.h"
#include "tcp_probe.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
constexpr char kHost[] = "192.168.10.26";
constexpr std::uint16_t kTcpPort = 24;
constexpr int kConnectTimeoutMs = 1000;
constexpr int kEventTimeoutMs = 30000;
constexpr int kDrainQuietMs = 100;
constexpr int kDrainMaximumMs = 1000;

const char* typeName(easiroc::DataType type) {
  switch (type) {
    case easiroc::DataType::kAdcHighGain:
      return "HG ADC";
    case easiroc::DataType::kAdcLowGain:
      return "LG ADC";
    case easiroc::DataType::kTdcLeading:
      return "Leading TDC";
    case easiroc::DataType::kTdcTrailing:
      return "Trailing TDC";
    case easiroc::DataType::kScaler:
      return "Scaler";
    case easiroc::DataType::kUnknown:
      return "Unknown";
  }
  return "Unknown";
}

void printEvent(const easiroc::Event& event) {
  std::size_t hg = 0;
  std::size_t lg = 0;
  std::size_t leading = 0;
  std::size_t trailing = 0;
  std::size_t scaler = 0;
  for (const auto& word : event.data) {
    switch (word.type) {
      case easiroc::DataType::kAdcHighGain:
        ++hg;
        break;
      case easiroc::DataType::kAdcLowGain:
        ++lg;
        break;
      case easiroc::DataType::kTdcLeading:
        ++leading;
        break;
      case easiroc::DataType::kTdcTrailing:
        ++trailing;
        break;
      case easiroc::DataType::kScaler:
        ++scaler;
        break;
      case easiroc::DataType::kUnknown:
        break;
    }
  }

  std::cout << "Event data size: " << event.data_size << " words ("
            << static_cast<std::size_t>(event.data_size) * 4 << " bytes)\n"
            << "HG ADC words: " << hg << '\n'
            << "LG ADC words: " << lg << '\n'
            << "Leading TDC words: " << leading << '\n'
            << "Trailing TDC words: " << trailing << '\n';
  if (scaler != 0)
    std::cout << "WARNING: received " << scaler
              << " Scaler word(s), although Scaler is OFF\n";

  for (std::size_t i = 0; i < event.data.size(); ++i) {
    const auto& word = event.data[i];
    std::cout << "word[" << i << "] " << typeName(word.type)
              << " channel=" << static_cast<unsigned>(word.channel)
              << " value=" << word.value;
    if (word.type == easiroc::DataType::kAdcHighGain ||
        word.type == easiroc::DataType::kAdcLowGain)
      std::cout << " overflow=" << (word.overflow ? "yes" : "no");
    std::cout << " raw=0x" << std::hex << std::setw(7) << std::setfill('0')
              << word.raw << std::dec << std::setfill(' ') << '\n';
  }
}
}  // namespace

int main() {
  const easiroc::DaqControl daq({true, true, false});
  const auto start = daq.startValue();
  const auto stop = daq.stopValue();
  RbcpClient rbcp(kHost);
  bool start_attempted = false;
  bool start_succeeded = false;
  bool operation_succeeded = false;
  std::string operation_error;
  std::unique_ptr<TcpConnection> tcp;

  try {
    std::cout << "Connecting to " << kHost << ':' << kTcpPort << "...\n";
    tcp = std::make_unique<TcpConnection>(kHost, kTcpPort, kConnectTimeoutMs);
    const std::size_t drained = tcp->drain(kDrainQuietMs, kDrainMaximumMs);
    std::cout << "Pre-acquisition drain: " << drained << " byte(s) discarded\n";

    start_attempted = true;
    std::cout << "RBCP write DAQ ON: address=0x" << std::hex
              << std::setw(8) << std::setfill('0') << start.address
              << " value=0x" << std::setw(2)
              << static_cast<unsigned>(start.value) << std::dec
              << std::setfill(' ') << '\n';
    rbcp.write(start.address, start.value);
    start_succeeded = true;

    easiroc::EventStreamParser parser;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(kEventTimeoutMs);
    bool received_event = false;
    while (!received_event) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline)
        throw std::runtime_error("event receive timeout after 30000 ms");
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - now)
                                 .count();
      const auto chunk = tcp->receive(4096, static_cast<int>(remaining));
      auto events = parser.push(chunk);
      if (!events.empty()) {
        printEvent(events.front());
        if (events.size() > 1)
          std::cout << "NOTICE: " << events.size() - 1
                    << " additional complete event(s) arrived in the same TCP chunk; ignored\n";
        received_event = true;
      }
    }
    operation_succeeded = true;
  } catch (const std::exception& error) {
    operation_error = error.what();
  }

  bool stop_succeeded = false;
  std::string stop_error;
  if (start_attempted) {
    try {
      std::cout << "RBCP write DAQ OFF: address=0x" << std::hex
                << std::setw(8) << std::setfill('0') << stop.address
                << " value=0x" << std::setw(2)
                << static_cast<unsigned>(stop.value) << std::dec
                << std::setfill(' ') << '\n';
      rbcp.write(stop.address, stop.value);
      stop_succeeded = true;
    } catch (const std::exception& error) {
      stop_error = error.what();
    }
  }
  tcp.reset();

  if (!start_succeeded && start_attempted)
    std::cerr << "ERROR: DAQ ON write failed; hardware DAQ state is unknown.\n";
  if (!operation_succeeded)
    std::cerr << "ERROR: " << operation_error << '\n';
  if (start_attempted && !stop_succeeded)
    std::cerr << "ERROR: DAQ OFF write failed; hardware DAQ state is unknown: "
              << stop_error << '\n';
  if (!operation_succeeded || (start_attempted && !stop_succeeded)) return 1;
  std::cout << "One event received and decoded; DAQ OFF confirmed.\n";
  return 0;
}
