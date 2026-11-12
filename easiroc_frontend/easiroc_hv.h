#ifndef EASIROC_HV_H
#define EASIROC_HV_H
#include <cstdint>
#include <functional>
#include <string>
namespace easiroc::hv {
constexpr double kMaxVoltage = 62;
constexpr double kDefaultCurrentLimitUa = 20;
constexpr double kNearZeroVoltage = 5;
// Normal descent only; Enable and unknown-setpoint admission stay at 1 V.
constexpr double kNormalDownTolerance = 5;
constexpr double kMaxCurrentUa = 0.0364 * 65535;
// Display only: post-sample wait <=2s, DAC <=1s, post-DAC wait <=2s,
// MADC <=1s. Control/protection continues to use fresh()'s existing 2s gate.
constexpr std::uint64_t kRampMeasurementMaxAgeMs = 6000;
constexpr std::uint64_t kSteadyMeasurementMaxAgeMs = 65000;
// Reviewed reference/firmware_20150923/rtl/HVControl.vhd:114-159.
// This verifies the reference logic, not the identity of installed firmware.
constexpr bool kEnableProtocolVerified = true;
constexpr const char* kBlockedReason = "Enable blocked: HV control protocol not verified";
constexpr const char* kAlreadyEnabledReason = "Enable unavailable: already enabled; use Voltage or Stop";
std::uint16_t dacCode(double voltage);
double voltage(std::uint16_t raw);
double currentUa(std::uint16_t raw);
bool validLimit(double limit);
struct Sample { double voltage = 0, current = 0; };
struct Io {
  std::function<void(std::uint32_t, std::uint8_t)> write;
  std::function<std::uint8_t(std::uint32_t)> read;
  std::function<void(unsigned)> delay;
};
Sample measure(const Io& io);
void setVoltage(const Io& io, double value);
void shutdown(const Io& io);
struct Context {
  bool registered = false, stopped = false, transition_clear = false;
  bool asic_applied = false, input_dac_enabled = false;
  bool frontend_enabled = false, asic_busy = true;
  bool enable_verified = false;
};
struct State {
  bool enabled_known = false, enabled = false, set_known = false;
  double set = 0, target = 0;
  Sample measured;
  bool valid = false, ramping = false, emergency = false, faulted = false;
  bool stop_after_ramp = false, settling = false;
  bool normal_down = false, down_step_pending = false;
  // Nonblocking phases: post-DAC sample, then post-measurement wait.
  bool step_sample_pending = false, step_wait = false;
  std::uint64_t updated = 0, last_tick = 0;
  std::uint64_t sampled_unix_ms = 0;
  std::string ramp_status = "Unknown", result = "Idle", error, monitoring = "Unknown";
};
struct Backend {
  std::function<Sample()> measure;
  // A successful DAC write may release FPGA shutdown. Never use it on an
  // unknown/disabled supply except the explicitly admitted Enable-at-zero.
  std::function<void(double)> set;
  std::function<void()> shutdown;
  // Monotonic milliseconds, used again after potentially blocking callbacks.
  std::function<std::uint64_t()> now;
  std::function<std::uint64_t()> unix_time;
  std::function<std::uint64_t()> unix_time_ms = {};
};
enum class Operation { Enable, Voltage, Stop, Emergency };
bool fresh(const State&, std::uint64_t now);
// ODB/display validity; never use this to admit a DAC or protection decision.
bool measurementFresh(const State&, std::uint64_t now);
bool request(State&, const Context&, Operation, double target, double limit,
             std::uint64_t now, const Backend&);
void tick(State&, const Context&, double limit, std::uint64_t now, const Backend&);
void fault(State&, const std::string& error);
}
#endif
