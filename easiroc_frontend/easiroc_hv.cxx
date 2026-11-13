#include "easiroc_hv.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace easiroc::hv {
std::uint16_t dacCode(double value) {
  if (!std::isfinite(value) || value < 0 || value > kMaxVoltage)
    throw std::invalid_argument("HV target must be 0..62 V");
  return static_cast<std::uint16_t>(value * 413.9 + 747.8);
}
double voltage(std::uint16_t raw) { return 0.00208 * raw + 0.0355; }
double currentUa(std::uint16_t raw) { return 0.0364 * raw; }
bool validLimit(double limit) { return std::isfinite(limit) && limit > 0 && limit < kMaxCurrentUa; }
namespace {
void madcInit(const Io& io) { io.write(0x10010,248); io.delay(10); io.write(0x10011,0); }
std::uint16_t raw(const Io& io, std::uint8_t channel) {
  io.write(0x10010,channel); io.delay(10); io.write(0x10011,1);
  io.write(0x10010,240); io.delay(50); io.write(0x10011,0); io.delay(10);
  const auto high = io.read(0x10020), low = io.read(0x10021);
  const auto value = (std::uint16_t(high) << 8) | low;
  if (value == 65535) throw std::runtime_error("MADC full scale: measurement may be saturated");
  return value;
}
bool access(const Context& c) {
  return c.registered && c.stopped && c.transition_clear && c.frontend_enabled && !c.asic_busy;
}
std::uint64_t completed(const Backend& b, std::uint64_t fallback) { return b.now ? b.now() : fallback; }
void sample(State& s, const Backend& b, std::uint64_t now) {
  const auto m = b.measure();
  if (!std::isfinite(m.voltage) || !std::isfinite(m.current) || m.voltage < 0 ||
      m.voltage >= voltage(65535) || m.current < 0 || m.current >= kMaxCurrentUa)
    throw std::runtime_error("Invalid/saturated MADC measurement");
  const auto end = completed(b,now);
  if (end < now || end-now > 1000) throw std::runtime_error("MADC transaction exceeded one-second budget");
  s.measured=m; s.valid=true; s.updated=end;
  s.sampled_unix_ms=b.unix_time_ms?b.unix_time_ms():b.unix_time?b.unix_time()*1000:0;
  s.monitoring=s.set_known && !(s.normal_down && !s.emergency) &&
      std::abs(m.voltage-s.set)>kNearZeroVoltage
      ? "WARNING: measured voltage differs from setpoint by more than 1 V" : "OK";
}
void writeSet(State& s, double value, const Backend& b, std::uint64_t now) {
  b.set(value);
  const auto end=completed(b,now);
  if (end < now || end-now > 1000) throw std::runtime_error("HV write exceeded one-second budget");
  s.last_tick=end; // Wait a full second after the completed DAC transaction.
  s.step_sample_pending=true; s.step_wait=false;
  s.set=value; s.set_known=true; s.enabled=true; s.enabled_known=true;
}
}
Sample measure(const Io& io) {
  io.write(0x10012,0); io.delay(10);
  madcInit(io);
  Sample result; result.voltage=voltage(raw(io,3)); result.current=currentUa(raw(io,4));
  io.write(0x10012,0); io.delay(10);
  return result;
}
void setVoltage(const Io& io, double value) {
  const auto code=dacCode(value);
  madcInit(io);
  io.write(0x10000,code >> 8); io.write(0x10001,code & 255); io.write(0x10002,1);
}
void shutdown(const Io& io) { io.write(0x10003,0); }
void fault(State& s, const std::string& error) {
  const bool completed_operation=!s.ramping && s.result.rfind("Succeeded",0)==0;
  s.faulted=true; s.valid=false; s.enabled_known=false; s.set_known=false;
  s.ramping=false; s.settling=false; s.stop_after_ramp=false;
  s.normal_down=false; s.down_step_pending=false;
  s.step_sample_pending=false; s.step_wait=false;
  s.ramp_status="Unknown"; s.monitoring=error;
  if (!completed_operation) {s.result="Indeterminate";s.error=error;}
}
bool fresh(const State& s, std::uint64_t now) {
  return s.valid && now>=s.updated && now-s.updated <= (s.ramping?2000u:65000u);
}
bool measurementFresh(const State& s, std::uint64_t now) {
  if (!s.valid || s.faulted || now<s.updated) return false;
  if (!s.ramping) return now-s.updated<=kSteadyMeasurementMaxAgeMs;
  // A normal wait can outlast control's sample gate, but neither a stalled
  // stage nor repeated stage progress may keep an old measurement alive.
  return now>=s.last_tick && now-s.last_tick<=2000 &&
      now-s.updated<=kRampMeasurementMaxAgeMs;
}
bool request(State& s, const Context& c, Operation operation, double target,
             double limit, std::uint64_t now, const Backend& b) {
  s.error.clear();
  auto reject=[&](const std::string& error){s.error=error;return false;};
  if (!access(c)) return reject("HV requires registered/enabled device, STOPPED, no transition and no ASIC Apply");
  try {
    if (operation==Operation::Emergency) {
      // Explicit operator request is allowed from Unknown/Fault, with no MADC
      // prerequisite and no DAC zero write (which could release shutdown).
      b.shutdown();
      s.enabled_known=true; s.enabled=false; s.faulted=false;
      s.ramping=false; s.settling=false; s.stop_after_ramp=false;
      s.normal_down=false; s.down_step_pending=false; s.emergency=false;
      s.step_sample_pending=false; s.step_wait=false;
      s.valid=false; s.ramp_status="Disabled"; s.result="Succeeded";
      s.last_tick=now>=1000?now-1000:0;
      return true;
    }
    if (s.faulted) return reject("HV fault latched: explicit emergency shutdown and state reconciliation required");
    if (!validLimit(limit)) return reject("CurrentLimit must be finite, positive and below ADC full scale");
    if (s.ramping && operation!=Operation::Stop) return reject("HV ramp busy; use normal stop or emergency shutdown to interrupt");
    const bool was_emergency=s.emergency && s.ramping;
    const bool pending_down=s.normal_down && s.down_step_pending && s.ramping;
    if (operation==Operation::Enable) {
      if (!c.enable_verified) return reject(kBlockedReason);
      if (target!=0 || !c.asic_applied || !c.input_dac_enabled)
        return reject("Enable requires target 0 V, applied ASIC and enabled InputDAC");
      // Legacy-compatible explicit Enable-at-zero also accepts Unknown or a
      // retained nonzero DAC. This is not proof of a safe initial output:
      // the first DAC byte can release shutdown before zero is transferred.
      if (s.enabled_known && s.enabled) return reject(kAlreadyEnabledReason);
      sample(s,b,now);
      if (s.measured.voltage>kEnableMaxResidualVoltage)
        return reject("Residual voltage exceeds " + std::to_string(kEnableMaxResidualVoltage) + " V");
      if (s.measured.current>limit) return reject("CurrentLimit exceeded; cannot Enable");
      writeSet(s,0,b,completed(b,now));
      s.target=0;
    } else {
      if (operation==Operation::Stop && (!s.enabled_known || !s.set_known)) {
        sample(s,b,now);
        if (s.measured.voltage>kNearZeroVoltage) return reject("Unknown setpoint: cannot safely ramp; use emergency shutdown");
        b.shutdown(); s.enabled_known=true; s.enabled=false; s.valid=false;
        s.ramp_status="Disabled"; s.result="Succeeded"; return true;
      }
      if (!s.enabled_known || !s.enabled || !s.set_known)
        return reject("Voltage ramp requires a known enabled supply and setpoint");
      dacCode(operation==Operation::Stop?0:target);
      if (!fresh(s,now)) sample(s,b,now);
      s.target=operation==Operation::Stop?0:target;
    }
    s.normal_down=operation==Operation::Stop || s.target<s.set;
    s.stop_after_ramp=operation==Operation::Stop || (s.normal_down && s.target==0);
    s.down_step_pending=s.normal_down && pending_down;
    // An accepted Stop cancels any deferred DAC, and rechecks the current
    // stage before scheduling descent. Rejected requests leave it untouched.
    s.step_sample_pending=s.step_sample_pending || s.step_wait;
    s.step_wait=false;
    s.ramping=true; s.emergency=was_emergency;
    if (s.normal_down && !s.emergency)
      s.monitoring=s.measured.current>limit?"WARNING: CurrentLimit exceeded":"OK";
    s.settling=s.set==s.target || s.down_step_pending;
    s.last_tick=completed(b,now);
    s.ramp_status=s.settling?"Settling":"Ramping"; s.result="Pending";
    return true;
  } catch (const std::exception& e) { fault(s,e.what()); s.result="Indeterminate";s.error=e.what();return false; }
}
void tick(State& s, const Context& c, double limit, std::uint64_t now, const Backend& b) {
  if (!access(c)) {
    if (s.ramping) fault(s,"Ramp interrupted by DAQ transition, registry change or ASIC Apply");
    s.valid=false; s.monitoring="HV monitoring unavailable: access conditions not met"; return;
  }
  if (s.faulted) return;
  // After shutdown take a new residual-voltage sample promptly, then every 30s.
  // During a ramp this is the post-DAC wait / arrival recheck interval.
  // Each sample gates the next stage; there is no independent 3-second poll.
  // Steady-state monitoring retains its existing 30-second cadence.
  const auto interval=s.ramping || (!s.valid && s.enabled_known && !s.enabled)?1000u:30000u;
  if (now<s.last_tick || (s.ramping && now-s.last_tick>2000)) {fault(s,"HV timing discontinuity");return;}
  if (now-s.last_tick<interval) return;
  s.last_tick=now;
  try {
    if (s.ramping && !validLimit(limit)) throw std::runtime_error("Invalid CurrentLimit; protection unavailable");
    if (!s.step_wait) sample(s,b,now);
    else if (!fresh(s,now)) throw std::runtime_error("Post-measurement wait lost valid sample");
    if (!validLimit(limit)) {s.monitoring="Invalid CurrentLimit; protection inactive";return;}
    const bool over=s.measured.current>limit;
    if (over) s.monitoring="WARNING: CurrentLimit exceeded";
    if (!s.ramping) return;
    if (!s.enabled_known || !s.enabled || !s.set_known) throw std::runtime_error("Ramp lost known output state");
    if (over && !s.emergency) {
      s.emergency=true;s.target=0;s.settling=false;s.down_step_pending=false;
      s.step_wait=false;
    }
    // Normal descent holds each acknowledged DAC setting until a subsequent
    // valid MADC sample is within +/-5 V. Protection descent bypasses this gate.
    if (s.normal_down && !s.emergency && s.down_step_pending) {
      if (std::abs(s.measured.voltage-s.set)>kNormalDownTolerance) {
        s.settling=true;s.ramp_status="Settling";
        return;
      }
      s.down_step_pending=false;
    }
    if (s.set!=s.target) {
      if (!s.emergency && s.step_sample_pending) {
        // Start once, after the successful MADC transaction / arrival check.
        // Return to MIDAS so Emergency can bypass this wait. The next tick
        // writes the next DAC without another sample or another one-second wait.
        s.step_sample_pending=false;s.step_wait=true;
        s.last_tick=completed(b,now);
        return;
      }
      const bool down=s.target<s.set;
      const double step=down?10:4;
      writeSet(s,s.set+std::clamp(s.target-s.set,-step,step),b,completed(b,now));
      if (s.normal_down && !s.emergency) {
        s.down_step_pending=true;
        // Keep writeSet's completion timestamp: the next MADC sample belongs
        // to this DAC stage, after its post-write wait, just as on ascent.
      }
      s.settling=s.down_step_pending || s.set==s.target;
      s.ramp_status=s.settling?"Settling":s.emergency?"EmergencyDown":"Ramping";
      return;
    }
    if (s.stop_after_ramp) {
      // No settling deadline. Communication/measurement/timing failures still
      // fault above. Overcurrent protection must never wait for voltage arrival.
      if (!s.emergency && s.measured.voltage>kNormalDownTolerance) {
        s.settling=true;s.ramp_status="Settling";
        return;
      }
      b.shutdown();s.enabled=false;s.enabled_known=true;s.valid=false;
    }
    s.ramping=false;s.settling=false;s.normal_down=false;s.down_step_pending=false;
    s.step_sample_pending=false;s.step_wait=false;
    s.ramp_status=s.enabled?"Steady":"Disabled";
    s.result=s.emergency?"Succeeded: CurrentLimit ramp to zero":"Succeeded";
    s.last_tick=completed(b,now);
  } catch (const std::exception& e) {fault(s,e.what());}
}
}
