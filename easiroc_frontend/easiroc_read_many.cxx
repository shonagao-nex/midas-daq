#include "easiroc_daq_control.h"
#include "easiroc_event.h"
#include "easiroc_stream.h"
#include "rbcp.h"
#include "tcp_probe.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
constexpr char kDefaultHost[] = "192.168.10.26";
constexpr std::uint16_t kTcpPort = 24;
constexpr std::size_t kDefaultEventCount = 100;
constexpr int kConnectTimeoutMs = 1000;
constexpr int kEventTimeoutMs = 30000;
constexpr int kDrainQuietMs = 100;
constexpr int kDrainMaximumMs = 1000;
constexpr std::size_t kChannelCount = 64;

struct Options {
  std::size_t requested_events = kDefaultEventCount;
  std::string host = kDefaultHost;
  bool verbose = false;
};

struct Moments {
  std::uint64_t count = 0;
  long double sum = 0;
  long double sum_squares = 0;

  void add(std::uint16_t value) {
    ++count;
    sum += value;
    sum_squares += static_cast<long double>(value) * value;
  }

  long double mean() const { return count == 0 ? 0 : sum / count; }
  long double rms() const {
    if (count == 0) return 0;
    const long double average = mean();
    const long double variance = sum_squares / count - average * average;
    return std::sqrt(variance > 0 ? variance : 0);
  }
};

struct Range {
  std::size_t minimum = std::numeric_limits<std::size_t>::max();
  std::size_t maximum = 0;

  void add(std::size_t value) {
    if (value < minimum) minimum = value;
    if (value > maximum) maximum = value;
  }
};

struct RunStatistics {
  std::size_t received = 0;
  std::size_t decoded = 0;
  std::size_t malformed_or_decode_errors = 0;
  std::size_t timeouts = 0;
  std::size_t disconnects = 0;
  Range event_data_size;
  Range hg_words;
  Range lg_words;
  std::uint64_t leading_words = 0;
  std::uint64_t trailing_words = 0;
  std::uint64_t scaler_words = 0;
  std::array<Moments, kChannelCount> hg;
  std::array<Moments, kChannelCount> lg;
};

std::size_t parseCount(const char* text) {
  if (text[0] == '\0' || text[0] == '-')
    throw std::invalid_argument("event count must be a positive integer");
  errno = 0;
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (errno == ERANGE || end == text || *end != '\0' || value == 0 ||
      value > std::numeric_limits<std::size_t>::max())
    throw std::invalid_argument("event count must be a positive integer");
  return static_cast<std::size_t>(value);
}

Options parseOptions(int argc, char** argv) {
  Options options;
  bool have_count = false;
  bool have_host = false;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--verbose" || argument == "-v") {
      options.verbose = true;
    } else if (!have_count) {
      options.requested_events = parseCount(argv[i]);
      have_count = true;
    } else if (!have_host) {
      options.host = argument;
      have_host = true;
    } else {
      throw std::invalid_argument("too many arguments");
    }
  }
  return options;
}

const char* typeName(easiroc::DataType type) {
  switch (type) {
    case easiroc::DataType::kAdcHighGain: return "HG ADC";
    case easiroc::DataType::kAdcLowGain: return "LG ADC";
    case easiroc::DataType::kTdcLeading: return "Leading TDC";
    case easiroc::DataType::kTdcTrailing: return "Trailing TDC";
    case easiroc::DataType::kScaler: return "Scaler";
    case easiroc::DataType::kUnknown: return "Unknown";
  }
  return "Unknown";
}

bool processEvent(const easiroc::Event& event, std::size_t event_number,
                  bool verbose, RunStatistics& statistics) {
  ++statistics.received;
  std::size_t hg = 0;
  std::size_t lg = 0;
  std::size_t leading = 0;
  std::size_t trailing = 0;
  std::size_t scaler = 0;
  std::size_t unknown = 0;
  std::array<unsigned, kChannelCount> hg_channels{};
  std::array<unsigned, kChannelCount> lg_channels{};

  for (const auto& word : event.data) {
    switch (word.type) {
      case easiroc::DataType::kAdcHighGain:
        ++hg;
        ++hg_channels[word.channel];
        statistics.hg[word.channel].add(word.value);
        break;
      case easiroc::DataType::kAdcLowGain:
        ++lg;
        ++lg_channels[word.channel];
        statistics.lg[word.channel].add(word.value);
        break;
      case easiroc::DataType::kTdcLeading: ++leading; break;
      case easiroc::DataType::kTdcTrailing: ++trailing; break;
      case easiroc::DataType::kScaler: ++scaler; break;
      case easiroc::DataType::kUnknown: ++unknown; break;
    }
  }

  statistics.event_data_size.add(event.data_size);
  statistics.hg_words.add(hg);
  statistics.lg_words.add(lg);
  statistics.leading_words += leading;
  statistics.trailing_words += trailing;
  statistics.scaler_words += scaler;

  bool valid = event.data.size() == event.data_size && hg == kChannelCount &&
               lg == kChannelCount && scaler == 0 && unknown == 0;
  for (std::size_t channel = 0; channel < kChannelCount; ++channel)
    valid = valid && hg_channels[channel] == 1 && lg_channels[channel] == 1;

  if (valid)
    ++statistics.decoded;
  else
    ++statistics.malformed_or_decode_errors;

  std::cout << "Event " << event_number << ": data=" << event.data_size
            << " HG=" << hg << " LG=" << lg << " leading=" << leading
            << " trailing=" << trailing << " scaler=" << scaler
            << " status=" << (valid ? "OK" : "INVALID") << '\n';
  if (!valid) {
    std::cerr << "ERROR: event " << event_number
              << " failed content validation"
              << " (expected one HG and one LG word per channel, no scaler or unknown words)\n";
  }

  if (verbose) {
    for (std::size_t i = 0; i < event.data.size(); ++i) {
      const auto& word = event.data[i];
      std::cout << "  word[" << i << "] " << typeName(word.type)
                << " channel=" << static_cast<unsigned>(word.channel)
                << " value=" << word.value;
      if (word.type == easiroc::DataType::kAdcHighGain ||
          word.type == easiroc::DataType::kAdcLowGain)
        std::cout << " overflow=" << (word.overflow ? "yes" : "no");
      std::cout << " raw=0x" << std::hex << std::setw(7)
                << std::setfill('0') << word.raw << std::dec
                << std::setfill(' ') << '\n';
    }
  }
  return valid;
}

void printRange(const char* label, const Range& range, bool have_events) {
  std::cout << label << ": ";
  if (have_events)
    std::cout << range.minimum << '/' << range.maximum << '\n';
  else
    std::cout << "n/a\n";
}

void printSummary(const Options& options, const RunStatistics& statistics) {
  std::cout << "\nRun summary\n"
            << "  requested events: " << options.requested_events << '\n'
            << "  received events: " << statistics.received << '\n'
            << "  successfully decoded events: " << statistics.decoded << '\n'
            << "  malformed / decode errors: "
            << statistics.malformed_or_decode_errors << '\n'
            << "  timeouts: " << statistics.timeouts << '\n'
            << "  disconnects: " << statistics.disconnects << '\n';
  printRange("  event data size min/max", statistics.event_data_size,
             statistics.received != 0);
  printRange("  HG ADC words min/max", statistics.hg_words,
             statistics.received != 0);
  printRange("  LG ADC words min/max", statistics.lg_words,
             statistics.received != 0);
  std::cout << "  Leading TDC words total: " << statistics.leading_words << '\n'
            << "  Trailing TDC words total: " << statistics.trailing_words << '\n'
            << "  Scaler words total: " << statistics.scaler_words << '\n'
            << "\nADC channel statistics (population RMS)\n"
            << "  ch     HG mean      HG RMS     LG mean      LG RMS\n"
            << std::fixed << std::setprecision(3);
  for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
    std::cout << std::setw(4) << channel << std::setw(12)
              << statistics.hg[channel].mean() << std::setw(12)
              << statistics.hg[channel].rms() << std::setw(12)
              << statistics.lg[channel].mean() << std::setw(12)
              << statistics.lg[channel].rms() << '\n';
  }
  std::cout.unsetf(std::ios::floatfield);
}
}  // namespace

int main(int argc, char** argv) {
  Options options;
  try {
    options = parseOptions(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << "\nUsage: " << argv[0]
              << " [event-count] [IP address] [--verbose]\n";
    return 2;
  }

  const easiroc::DaqControl daq({true, true, false});
  const auto start = daq.startValue();
  const auto stop = daq.stopValue();
  RbcpClient rbcp(options.host);
  RunStatistics statistics;
  bool start_attempted = false;
  bool start_succeeded = false;
  bool operation_succeeded = false;
  std::string operation_error;
  std::unique_ptr<TcpConnection> tcp;

  try {
    std::cout << "Connecting to " << options.host << ':' << kTcpPort << "...\n";
    tcp = std::make_unique<TcpConnection>(options.host, kTcpPort,
                                          kConnectTimeoutMs);
    const std::size_t drained = tcp->drain(kDrainQuietMs, kDrainMaximumMs);
    std::cout << "Pre-acquisition drain: " << drained << " byte(s) discarded\n";

    start_attempted = true;
    std::cout << "RBCP write DAQ ON: address=0x" << std::hex << std::setw(8)
              << std::setfill('0') << start.address << " value=0x"
              << std::setw(2) << static_cast<unsigned>(start.value) << std::dec
              << std::setfill(' ') << '\n';
    rbcp.write(start.address, start.value);
    start_succeeded = true;

    easiroc::EventStreamParser parser;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(kEventTimeoutMs);
    while (statistics.received < options.requested_events) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        ++statistics.timeouts;
        throw std::runtime_error("event receive timeout after 30000 ms");
      }
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 deadline - now).count();
      std::vector<std::uint8_t> chunk;
      try {
        chunk = tcp->receive(4096, static_cast<int>(remaining));
      } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find("timeout") != std::string::npos)
          ++statistics.timeouts;
        else
          ++statistics.disconnects;
        throw;
      }

      std::vector<easiroc::Event> events;
      try {
        events = parser.push(chunk);
      } catch (...) {
        ++statistics.malformed_or_decode_errors;
        throw;
      }
      for (const auto& event : events) {
        if (!processEvent(event, statistics.received + 1, options.verbose,
                          statistics))
          throw std::runtime_error("event content validation failed");
      }
      if (!events.empty())
        deadline = std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(kEventTimeoutMs);
    }
    operation_succeeded = statistics.malformed_or_decode_errors == 0;
    if (!operation_succeeded)
      operation_error = "one or more events failed validation";
  } catch (const std::exception& error) {
    operation_error = error.what();
  }

  bool stop_succeeded = false;
  std::string stop_error;
  if (start_attempted) {
    try {
      std::cout << "RBCP write DAQ OFF: address=0x" << std::hex << std::setw(8)
                << std::setfill('0') << stop.address << " value=0x"
                << std::setw(2) << static_cast<unsigned>(stop.value) << std::dec
                << std::setfill(' ') << '\n';
      rbcp.write(stop.address, stop.value);
      stop_succeeded = true;
    } catch (const std::exception& error) {
      stop_error = error.what();
    }
  }
  tcp.reset();
  printSummary(options, statistics);

  if (!start_succeeded && start_attempted)
    std::cerr << "ERROR: DAQ ON write failed; hardware DAQ state is unknown.\n";
  if (!operation_succeeded)
    std::cerr << "ERROR: " << operation_error << '\n';
  if (start_attempted && !stop_succeeded)
    std::cerr << "ERROR: DAQ OFF write failed; hardware state unknown: "
              << stop_error << '\n';
  if (!operation_succeeded || (start_attempted && !stop_succeeded)) return 1;
  std::cout << "Requested events received and decoded; DAQ OFF confirmed.\n";
  return 0;
}
