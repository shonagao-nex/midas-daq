#include "easiroc_hv.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <utility>
using namespace easiroc::hv;
int main() {
  int checks=0;
  auto check=[&](bool ok,const char* reason){++checks;
      if(!ok)throw std::runtime_error(reason);};
  try {
    check(dacCode(0)==747 && dacCode(62)==26409,"DAC endpoints");
    check(std::abs(voltage(1000)-2.1155)<1e-9 && std::abs(currentUa(1000)-36.4)<1e-9,"conversion");
    for(double v:{-1.,62.01,INFINITY*1.,NAN*1.}) {bool failed=false;try{dacCode(v);}catch(...){failed=true;}check(failed,"invalid voltage");}
    check(validLimit(10) && !validLimit(0) && !validLimit(kMaxCurrentUa) && !validLimit(NAN),"limit excludes full scale");
    std::vector<std::pair<std::uint32_t,std::uint8_t>> writes;
    std::vector<unsigned> delays;
    Io io{[&](auto a,auto v){writes.emplace_back(a,v);},[](auto a)->std::uint8_t{return a==0x10020?3:232;},[&](auto d){delays.push_back(d);}};
    auto m=measure(io);
    check(std::abs(m.current-36.4)<1e-9,"MADC bytes");
    const std::vector<std::pair<std::uint32_t,std::uint8_t>> golden={{0x10012,0},{0x10010,248},{0x10011,0},{0x10010,3},{0x10011,1},{0x10010,240},{0x10011,0},{0x10010,4},{0x10011,1},{0x10010,240},{0x10011,0},{0x10012,0}};
    check(writes==golden && delays==std::vector<unsigned>({10,10,10,50,10,10,50,10,10}),"MUX/control timing");
    writes.clear();setVoltage(io,62);check(writes==std::vector<std::pair<std::uint32_t,std::uint8_t>>({{0x10010,248},{0x10011,0},{0x10000,103},{0x10001,41},{0x10002,1}}),"DAC write order");
    writes.clear();shutdown(io);check(writes==std::vector<std::pair<std::uint32_t,std::uint8_t>>({{0x10003,0}}),"shutdown never writes DAC");
    for(unsigned fail=0;fail<golden.size();++fail) {
      unsigned calls=0;auto bad=io;bad.write=[&](auto,auto){if(calls++==fail)throw std::runtime_error("ACK lost");};
      bool failed=false;try{measure(bad);}catch(...){failed=true;}check(failed && calls==fail+1,"failed write must not run cleanup");
    }
    for(int fail=0;fail<2;++fail) {
      auto bad=io;int reads=0;bad.read=[&](auto)->std::uint8_t{if(reads++==fail)throw std::runtime_error("read failed");return 0;};
      bool failed=false;try{measure(bad);}catch(...){failed=true;}check(failed,"read failure propagated");
    }
    auto saturated=io;saturated.read=[](auto)->std::uint8_t{return 255;};
    bool clipped=false;try{measure(saturated);}catch(...){clipped=true;}check(clipped,"saturation rejected");
    Context c{true,true,true,true,true,true,false,true};
    check(kDefaultCurrentLimitUa==20 && kNormalDownTolerance==5,
          "default current limit and normal descent tolerance");
    {
      State t;
      double current=20;unsigned programs=0;
      Backend io20{[&]{return Sample{0.1,current};},[&](double){++programs;},[]{},{},{}};
      check(request(t,c,Operation::Enable,0,kDefaultCurrentLimitUa,0,io20) && programs==1,
            "20 uA inclusive default permits Enable at zero");
      State rejected;current=20.001;
      check(!request(rejected,c,Operation::Enable,0,kDefaultCurrentLimitUa,0,io20) && programs==1,
            "above 20 uA blocks Enable before any DAC write");
    }
    // First Enable: no prior shutdown, known DAC, or manufactured valid sample.
    {
      State initial;unsigned measures=0,programs=0,disables=0;double output=0.5;
      Backend first_io{[&]{++measures;return Sample{output,1};},[&](double v){++programs;output=v;},[&]{++disables;},{},{}};
      check(!initial.enabled_known && !initial.set_known && !initial.valid,"initial Unknown is not a measurement");
      check(request(initial,c,Operation::Enable,0,10,0,first_io) && programs==1 && disables==0 && measures==1,
            "first Enable accepts Unknown without prior shutdown");
      check(initial.enabled_known && initial.enabled && initial.set_known && initial.set==0 && initial.target==0 && initial.ramping,
            "zero command establishes command-derived state and enters settling");
      check(!request(initial,c,Operation::Voltage,2,10,0,first_io),"voltage request must wait for Enable settling");
      tick(initial,c,10,1000,first_io);check(!initial.ramping,"Enable completes after post-write current check");
      tick(initial,c,10,2000,first_io);check(!initial.ramping && initial.result=="Succeeded","first Enable completes after measurements");
      check(request(initial,c,Operation::Voltage,2,10,2000,first_io),"first Enable to voltage request");
      tick(initial,c,10,3000,first_io);check(initial.set==2 && initial.ramping,"first upward step is 2 V");
      tick(initial,c,10,4000,first_io);tick(initial,c,10,5000,first_io);
      check(initial.result=="Succeeded" && !initial.ramping && disables==0,"first Enable/ramp lifecycle without implicit shutdown");
    }
    // Deterministic monotonic clock includes callback latency: each DAC
    // completion must precede its current check by a full second.
    for (double target : {2.,18.,62.}) {
      State t;std::uint64_t clock=0, written=0, measured=0;double setting=0;
      std::vector<double> steps;bool pending=false;
      Backend timed;
      timed.now=[&]{return clock;};
      timed.measure=[&]{
        if(pending) {check(clock-written>=1000,"post-write wait before MADC");pending=false;}
        clock+=170;measured=clock;
        return Sample{0.1,10}; // exact limit, deliberately no voltage arrival
      };
      timed.set=[&](double v){
        check(!pending,"no DAC before previous current check");
        if(steps.size()>1) check(clock-measured>=1000,"next upward DAC waits a full second after MADC completion");
        clock+=100;written=clock;pending=true;setting=v;steps.push_back(v);
      };
      timed.shutdown=[]{};
      check(request(t,c,Operation::Enable,0,10,clock,timed),"timed Enable");
      clock=written+999;tick(t,c,10,clock,timed);check(pending,"no early MADC");
      ++clock;tick(t,c,10,clock,timed);check(!t.ramping,"Enable current checked");
      check(request(t,c,Operation::Voltage,target,10,clock,timed),"timed voltage request");
      while(t.ramping) {
        clock=t.last_tick+999;auto size=steps.size();tick(t,c,10,clock,timed);
        check(steps.size()==size,"no early DAC");
        ++clock;tick(t,c,10,clock,timed);
      }
      std::vector<double> expected{0};
      for(double v=4;v<target;v+=4)expected.push_back(v);
      expected.push_back(target);
      check(steps==expected && setting==target && !pending,"complete 0/2/18/62 step sequence");
      check(t.result=="Succeeded" && t.monitoring.find("WARNING")!=std::string::npos,"mismatch warning preserves success");
      clock=t.last_tick+30000;tick(t,c,10,clock,timed);
      check(steps==expected,"no automatic repeated ramp");
    }
    for(int failure=0;failure<3;++failure) {
      State t;t.enabled_known=t.enabled=t.set_known=t.ramping=true;
      t.set=18;t.target=62;t.last_tick=1000;
      std::vector<double> commands;
      Backend guarded{[&]()->Sample {
        if(failure==0)throw std::runtime_error("MADC timeout");
        return Sample{0.1,failure==1?10.0001:10};
      },[&](double v){commands.push_back(v);if(failure==2)throw std::runtime_error("DAC ACK lost");},[]{},{},{}};
      tick(t,c,10,2000,guarded);
      if(failure==1) {
        check(commands==std::vector<double>{8} && t.emergency && t.target==0,"strict greater-than limit selects down only");
        guarded.measure=[] {return Sample{0.1,1};};
        tick(t,c,10,3000,guarded);tick(t,c,10,4000,guarded);
        check(commands==std::vector<double>({8,0}) && !t.ramping,"overcurrent down 10 V/step to zero with final check");
        tick(t,c,10,34000,guarded);check(commands.size()==2,"overcurrent never resumes automatically");
      } else {
        const auto attempts=commands.size();
        check(t.faulted && (failure!=0 || commands.empty()),"MADC failure prohibits next DAC");
        tick(t,c,10,3000,guarded);check(commands.size()==attempts,"communication failure prohibits further steps");
      }
    }
    State s;double physical=0.5,load=1;int samples=0,sets=0,shutdowns=0;
    Backend b{[&]{++samples;return Sample{physical,load};},[&](double v){++sets;physical=v;},[&]{++shutdowns;},{},{}};
    check(!s.enabled_known && !s.set_known && !s.valid,"Unknown is not promoted to OFF/zero/valid");
    check(request(s,c,Operation::Emergency,0,0,0,b) && shutdowns==1 && sets==0 && samples==0,"emergency immediate, no measurement/limit dependency");
    check(!s.valid && s.enabled_known && !s.enabled,"shutdown retains voltage uncertainty");
    tick(s,c,10,1000,b);check(s.valid && s.measured.voltage==0.5,"residual sample after shutdown");
    check(kEnableProtocolVerified,"reference RTL must be reviewed");
    check(!s.set_known,"shutdown does not establish the retained DAC value");
    for(int guard=0;guard<8;++guard) {
      auto unsafe=c;
      if(guard==0)unsafe.registered=false;
      if(guard==1)unsafe.stopped=false;
      if(guard==2)unsafe.transition_clear=false;
      if(guard==3)unsafe.asic_applied=false;
      if(guard==4)unsafe.input_dac_enabled=false;
      if(guard==5)unsafe.frontend_enabled=false;
      if(guard==6)unsafe.asic_busy=true;
      if(guard==7)unsafe.enable_verified=false;
      check(!request(s,unsafe,Operation::Enable,0,10,1000,b),"Enable guards");
    }
    check(!request(s,c,Operation::Enable,1,10,1000,b),"Enable target must zero");
    check(!request(s,c,Operation::Enable,0,0,1000,b),"Enable needs configured limit");
    physical=5.001;
    check(!request(s,c,Operation::Enable,0,10,1000,b) && sets==0 &&
          s.error=="Residual voltage exceeds 5 V", "above 5 V residual blocks Enable before DAC write");
    physical=5;load=10.001;
    check(!request(s,c,Operation::Enable,0,10,1000,b) && sets==0,"overcurrent blocks Enable before DAC write");
    load=10;check(request(s,c,Operation::Enable,0,10,1000,b),"inclusive 5 V residual and DAC re-enable");
    load=1;
    check(s.enabled && s.set==0 && s.ramping && sets==1,"Enable remains settling");
    tick(s,c,10,2000,b);check(!s.ramping,"Enable post-write current check");
    tick(s,c,10,3000,b);check(!s.ramping && s.result=="Succeeded","two final measurements");
    check(request(s,c,Operation::Voltage,6,10,3000,b),"ramp accepted");
    tick(s,c,10,3999,b);check(s.set==0,"no early step");
    tick(s,c,10,4000,b);check(s.set==4,"up 4 V/step");
    tick(s,c,10,5000,b);check(s.set==4 && s.step_wait,"successful current check starts post-measurement wait");
    tick(s,c,10,6000,b);check(s.set==6 && s.ramping,"final partial step stays protected");
    physical=0.1;tick(s,c,10,7000,b);
    check(!s.ramping && s.result=="Succeeded" && s.monitoring.find("WARNING")!=std::string::npos,
          "low measured voltage warns without blocking completion");
    tick(s,c,10,9000,b);
    const int count=samples;tick(s,c,10,36999,b);check(samples==count,"30 second cadence");
    load=10;tick(s,c,10,37000,b);check(s.result=="Succeeded","equal limit is not exceeded");
    load=10.01;const int prior_sets=sets;tick(s,c,10,69000,b);check(sets==prior_sets && s.result=="Succeeded" && s.monitoring.find("WARNING")!=std::string::npos,"steady warning only");
    load=1;check(request(s,c,Operation::Stop,0,10,69000,b),"normal stop");
    tick(s,c,10,70000,b);check(s.set==0,"normal descent clamps 10 V step at zero");
    physical=5.001;tick(s,c,10,71000,b);tick(s,c,10,72000,b);
    check(s.set==0 && shutdowns==1,"no shutdown before measured zero");
    tick(s,c,10,73000,b);check(shutdowns==1,"normal stop waits above 5 V");
    physical=5;tick(s,c,10,74000,b);check(shutdowns==2 && !s.enabled && !s.ramping,"normal stop inclusive threshold");
    check(!s.valid,"shutdown does not assert physical zero");
    physical=0.8;tick(s,c,10,76000,b);check(s.valid && s.measured.voltage==0.8,"residual after Disable");
    physical=0;check(request(s,c,Operation::Enable,0,10,76000,b),"re-Enable clears shutdown via zero DAC");
    tick(s,c,10,77000,b);tick(s,c,10,78000,b);
    // Force a final-step surge. It must still trigger emergency ramp-down.
    s.set=30;s.target=32;physical=30;s.ramping=true;s.last_tick=78000;
    tick(s,c,10,79000,b);check(s.set==32 && s.ramping,"final-step protection retained");
    load=11;tick(s,c,10,80000,b);check(s.set==22 && s.target==0 && s.emergency,"10 V emergency decrement");
    load=1;tick(s,c,10,81000,b);check(s.set==12,"emergency latched after current falls");
    tick(s,c,10,82000,b);tick(s,c,10,83000,b);check(s.set==0 && s.ramping,"emergency clamps at zero");
    tick(s,c,10,84000,b);tick(s,c,10,85000,b);check(!s.ramping && s.enabled,"overcurrent lowers target without implicit shutdown");
    for(int mode=0;mode<4;++mode) {
      State t;t.enabled=t.enabled_known=t.set_known=t.ramping=true;t.target=4;t.last_tick=100000;
      auto bad=b;
      if(mode==0)bad.measure=[]()->Sample{throw std::runtime_error("timeout");};
      if(mode==1)bad.set=[](double){throw std::runtime_error("ACK lost");};
      if(mode==2)bad.measure=[](){return Sample{NAN,0};};
      if(mode==3)bad.now=[](){return 102001u;};
      tick(t,c,10,101000,bad);check(t.faulted && !t.enabled_known && !t.set_known && !t.valid,"communication/invalid/timing failure Unknown");
      const int before=samples;tick(t,c,10,131000,b);check(samples==before,"fault prevents automatic recovery");
      check(request(t,c,Operation::Emergency,0,10,131000,b),"explicit shutdown can recover fault");
    }
    for(int mode=0;mode<3;++mode) {
      State t;t.ramping=true;t.last_tick=1000;auto unsafe=c;
      if(mode==0)unsafe.stopped=false;
      if(mode==1)unsafe.transition_clear=false;
      if(mode==2)unsafe.asic_busy=true;
      const int before=samples+sets+shutdowns;tick(t,unsafe,10,2000,b);
      check(t.faulted && before==samples+sets+shutdowns,"DAQ/Apply interrupt: zero I/O");
      check(!request(t,unsafe,Operation::Emergency,0,10,2000,b),"emergency also forbidden while RUN/transition/Apply");
    }
    State restarted;check(!restarted.enabled_known && !restarted.set_known && !restarted.valid,"restart starts Unknown");
    State unknown;physical=2;check(!request(unknown,c,Operation::Stop,0,10,1000,b),"unknown setpoint cannot ramp a residual supply");
    physical=1;check(request(unknown,c,Operation::Stop,0,10,1000,b) && !unknown.enabled,"unknown near-zero supply can shutdown without DAC");
    State interrupted;interrupted.enabled_known=interrupted.enabled=interrupted.set_known=interrupted.ramping=true;
    interrupted.set=12;interrupted.target=62;interrupted.last_tick=1000;physical=12;
    check(request(interrupted,c,Operation::Stop,0,10,1000,b),"normal stop may interrupt a ramp");
    tick(interrupted,c,10,2000,b);check(interrupted.set==2 && interrupted.stop_after_ramp,"interrupted ramp descends at 10 V/step");
    interrupted.emergency=true;
    check(request(interrupted,c,Operation::Stop,0,10,2000,b) && interrupted.emergency,"normal stop preserves emergency latch");
    tick(interrupted,c,10,3000,b);check(interrupted.set==0,"preserved emergency rate");
    State high_dac;high_dac.enabled_known=high_dac.enabled=high_dac.set_known=true;
    high_dac.set=62;physical=0.4;
    const int high_sets=sets;
    check(request(high_dac,c,Operation::Emergency,0,10,1000,b) && high_dac.set==62,
          "emergency shutdown preserves retained nonzero DAC knowledge");
    check(request(high_dac,c,Operation::Enable,0,10,1000,b) && sets==high_sets+1 && high_dac.set==0 && high_dac.ramping,
          "explicit re-Enable accepts a retained nonzero DAC and programs target zero");
    tick(high_dac,c,10,2000,b);tick(high_dac,c,10,3000,b);
    check(!high_dac.ramping && request(high_dac,c,Operation::Voltage,2,10,3000,b),"emergency/re-Enable can proceed to ramp");
    tick(high_dac,c,10,4000,b);check(high_dac.set==2,"ramp after emergency/re-Enable");
    // A new frontend state does not trust the previous known command state.
    State after_restart;physical=0.4;
    check(!after_restart.valid && !after_restart.enabled_known && !after_restart.set_known,"restart remains Unknown before explicit Enable");
    check(request(after_restart,c,Operation::Enable,0,10,5000,b) && after_restart.set==0,
          "restart Unknown alone does not prohibit Enable");
    tick(after_restart,c,10,6000,b);tick(after_restart,c,10,7000,b);
    check(!after_restart.ramping && after_restart.result=="Succeeded","restart Enable final measurements");
    State failed_shutdown;auto bad_shutdown=b;bad_shutdown.shutdown=[](){throw std::runtime_error("shutdown ACK lost");};
    check(!request(failed_shutdown,c,Operation::Emergency,0,10,1000,bad_shutdown) && failed_shutdown.faulted && !failed_shutdown.enabled_known,"failed shutdown must be Unknown");
    for (double initial : {10.,62.}) {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=initial;
      double actual=initial;unsigned disables=0,reads=0;std::uint64_t clock=1000;
      std::vector<double> steps;
      Backend delayed{[&]{++reads;clock+=170;return Sample{actual,10};},
        [&](double v){steps.push_back(v);clock+=10;},[&]{++disables;},[&]{return clock;},{}};
      check(request(t,c,Operation::Stop,0,10,clock,delayed),"normal Stop accepted");
      std::uint64_t tick_time=2000;
      while(t.set>0) {
        clock=tick_time;tick(t,c,10,clock,delayed);
        check(t.set==std::max(0.,initial-10*steps.size()) && t.ramping && t.result=="Pending", "normal down 10 V step stays Pending");
        const auto programmed=steps.size();const auto update=t.updated;
        tick_time=t.last_tick;
        // Both high and low out-of-band samples hold this exact setting.
        for (double residual : {t.set+5.001,t.set>6?t.set-5.001:t.set+6}) {
          actual=residual;clock=tick_time+1000;tick(t,c,10,clock,delayed);tick_time+=1000;
          check(steps.size()==programmed && t.ramping && !t.faulted && t.updated>update && t.monitoring=="OK",
                "out of band voltage holds DAC, updates MADC, adds no tracking warning");
        }
        // Keep measuring for over 60 seconds at every step; no timeout.
        actual=t.set+6;
        for(int n=0;n<65;++n) {clock=tick_time+1000;tick(t,c,10,clock,delayed);tick_time+=1000;}
        check(steps.size()==programmed && t.ramping && !t.faulted && t.result=="Pending" && disables==0,
              "normal wait exceeds 60 seconds without fault or premature success");
        // Exactly +/-5 V is inclusive; zero must use <=5 V.
        actual=t.set==0?5:t.set+(programmed%2 || t.set<5?5:-5);
        tick_time+=1000;
        clock=tick_time;tick(t,c,10,clock,delayed);
        if(t.set==0) {
          check(!t.ramping && disables==1 && !t.enabled && t.result=="Succeeded","zero measurement precedes Disable and success");
        } else {
          check(t.step_wait && steps.size()==programmed,"arrival starts one post-measurement wait");
          tick_time=t.last_tick+1000;
        }
        // The next loop writes the next DAC after the post-measurement wait.
      }
      std::vector<double> expected;
      for(double v=initial;v>0;) {v=std::max(0.,v-10);expected.push_back(v);}
      check(steps==expected && reads>65*steps.size(),"10/62 to zero complete sequence with one-second monitoring");
    }
    // A partial Voltage reduction ends at its target; a reduction to zero
    // follows the same measured-zero then Disable contract as Normal Stop.
    for(double target : {43.,0.}) {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=50;
      double actual=50;unsigned disables=0;std::vector<double> steps;
      Backend down{[&]{return Sample{actual,1};},[&](double v){steps.push_back(v);},[&]{++disables;},{},{}};
      check(request(t,c,Operation::Voltage,target,10,0,down),"Voltage descent accepted");
      for(std::uint64_t ms=1000;t.ramping;ms+=1000) {
        actual=t.set;tick(t,c,10,ms,down);
      }
      check(t.set==target && t.result=="Succeeded" && disables==(target==0?1u:0u),"Voltage descent completes requested target/zero shutdown");
    }
    for(int mode=0;mode<6;++mode) {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=62;
      double actual=62,load=1;unsigned disables=0;std::vector<double> steps;
      Backend down{[&]()->Sample {
        if(mode==2 && !steps.empty())throw std::runtime_error("MADC timeout");
        return Sample{mode==3 && !steps.empty()?NAN:actual,load};
      },[&](double v){steps.push_back(v);if(mode==4)throw std::runtime_error("DAC ACK lost");},[&]{++disables;},{},{}};
      check(request(t,c,Operation::Stop,0,10,0,down),"waiting Stop accepted");
      tick(t,c,10,1000,down); // 62 -> 52, MADC deliberately remains at 62
      if(mode>=2 && mode<=4) {
        tick(t,c,10,2000,down);check(t.faulted && steps.size()==1,"MADC/invalid/DAC failure forbids next normal down step");
        tick(t,c,10,3000,down);check(steps.size()==1,"fault never retries DAC");continue;
      }
      for(std::uint64_t ms=2000;ms<=70000;ms+=1000)tick(t,c,10,ms,down);
      check(t.ramping && t.set==52 && steps.size()==1 && t.monitoring=="OK","long pending intermediate descent");
      if(mode==5) {
        // Overcurrent must bypass the normal voltage gate at once and remain
        // latched even when current falls. It must not wait at final zero.
        load=10.001;tick(t,c,10,71000,down);check(t.emergency && t.set==42,"overcurrent bypasses unsettled normal step");
        load=1;
        for(std::uint64_t ms=72000;ms<=77000;ms+=1000)tick(t,c,10,ms,down);
        check(steps==std::vector<double>({52,42,32,22,12,2,0}) && !t.ramping && disables==1,
              "protective descent reaches zero and shuts down without voltage wait");
      } else {
        if(mode==1) {actual=NAN;load=NAN;}
        check(request(t,c,Operation::Emergency,0,0,70000,down) && disables==1 && !t.ramping && !t.down_step_pending,
              "Emergency ignores sample/limit during long Stop wait and cancels descent");
        for(std::uint64_t ms=71000;ms<=140000;ms+=1000)tick(t,c,10,ms,down);
        check(steps.size()==1 && disables==1,"Emergency never resumes old Stop/DAC writes");
      }
    }
    {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=62;
      double actual=62;std::vector<double> commands;
      Backend down{[&]{return Sample{actual,1};},[&](double v){commands.push_back(v);},[]{},{},{}};
      check(request(t,c,Operation::Stop,0,10,0,down),"first Stop for repeated-request test");
      tick(t,c,10,1000,down);
      check(request(t,c,Operation::Stop,0,10,1000,down),"repeated Stop accepted");
      tick(t,c,10,2000,down);
      check(commands==std::vector<double>{52} && t.down_step_pending,"repeated Stop cannot bypass pending step gate");
      actual=53;tick(t,c,10,3000,down);
      check(commands.size()==1 && t.step_wait,"repeated Stop still waits after arrival");
      tick(t,c,10,4000,down);
      check(commands==std::vector<double>({52,42}),"inclusive upper boundary advances after repeated Stop");
    }
    {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=10;
      double actual=10;
      Backend down{[&]{return Sample{actual,1};},[](double){},[]{throw std::runtime_error("shutdown ACK lost");},{},{}};
      check(request(t,c,Operation::Stop,0,10,0,down),"normal Stop before failed shutdown");
      tick(t,c,10,1000,down);actual=1;tick(t,c,10,2000,down);
      check(t.faulted && t.result=="Indeterminate" && !t.enabled_known,"normal Stop never succeeds if Disable acknowledgement fails");
    }
    // Inject a surge after each ascending DAC stage, including the final one.
    for(double surge_at : {4.,8.,12.}) {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;
      std::vector<double> commands;unsigned reads=0;
      Backend staged{[&]{++reads;return Sample{t.set,t.set==surge_at?10.001:10};},
        [&](double v){commands.push_back(v);},[]{},{},{}};
      check(request(t,c,Operation::Voltage,12,10,0,staged),"stage surge request");
      std::uint64_t ms=0;
      while(t.set<surge_at) {ms+=1000;tick(t,c,10,ms,staged);}
      const auto before=reads;ms+=1000;tick(t,c,10,ms,staged);
      check(reads==before+1 && t.emergency && t.target==0 && t.set<surge_at && t.ramping,
            "each ascent stage checks current before any next step or success");
      check(commands.back()==std::max(0.,surge_at-10),"surge selects protective 10 V descent");
    }
    // Include callback latency: each normal-down measurement follows the
    // completed DAC write by a second, rather than an independent 3s timer.
    {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=20;
      std::uint64_t clock=0,written=0;double actual=20;
      unsigned reads=0,disables=0;std::vector<double> commands;
      Backend staged{[&]{++reads;clock+=170;return Sample{actual,10};},
        [&](double v){commands.push_back(v);clock+=100;written=clock;},
        [&]{++disables;},[&]{return clock;},{}};
      check(request(t,c,Operation::Stop,0,10,clock,staged),"timed down request");
      clock=1000;tick(t,c,10,clock,staged);
      check(commands==std::vector<double>{10} && t.last_tick==written,"down wait starts at DAC completion");
      const auto before=reads;
      clock=written+999;tick(t,c,10,clock,staged);
      check(reads==before,"no down sample before post-write wait");
      actual=15.001;clock=written+1000;tick(t,c,10,clock,staged);
      check(reads==before+1 && commands.size()==1 && t.ramping,"out-of-band down sample holds stage");
      auto retry=t.last_tick+1000;
      clock=retry-1;tick(t,c,10,clock,staged);
      check(reads==before+1,"arrival recheck waits one second");
      actual=5;clock=retry;tick(t,c,10,clock,staged);
      check(commands.size()==1 && t.step_wait,"arrival measurement starts post-measurement down wait");
      const auto measured=reads;const auto arrived=t.updated;
      clock=arrived+999;tick(t,c,10,clock,staged);
      check(commands.size()==1 && reads==measured,"no DAC or repeated MADC before down wait expires");
      clock=arrived+1000;tick(t,c,10,clock,staged);
      check(reads==measured,"post-measurement wait ends with DAC, not another measurement");
      check(commands==std::vector<double>({10,0}) && disables==0,"minus five boundary advances to final DAC only");
      actual=5.001;clock=written+1000;tick(t,c,10,clock,staged);
      check(t.ramping && disables==0 && t.result=="Pending","final zero setting waits above five volts");
      actual=5;clock=t.last_tick+1000;tick(t,c,10,clock,staged);
      check(!t.ramping && disables==1 && t.result=="Succeeded","final valid measurement at five volts precedes Shutdown");
    }
    // Emergency must bypass both waits in either direction, with no MADC
    // prerequisite, even halfway through the newly introduced wait.
    for(bool down : {false,true}) for(bool after_measure : {false,true}) {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=down?30:0;
      unsigned reads=0,disables=0;std::vector<double> commands;
      Backend interruptible{[&]{++reads;return Sample{t.set,20};},
        [&](double v){commands.push_back(v);},[&]{++disables;},{},{}};
      check(request(t,c,down?Operation::Stop:Operation::Voltage,down?0:12,20,0,interruptible),"interruptible ramp admitted");
      tick(t,c,20,1000,interruptible);
      if(after_measure) {
        tick(t,c,20,2000,interruptible);
        check(t.step_wait && commands.size()==1,"safe measurement defers next DAC");
      }
      const auto before=reads;const auto timestamp=t.updated;
      const auto at=after_measure?2500u:1500u;
      tick(t,c,20,at,interruptible);
      check(reads==before && t.updated==timestamp && commands.size()==1,"waiting tick returns with zero hardware I/O");
      auto broken=interruptible;broken.measure=[]()->Sample{throw std::runtime_error("must not measure before Emergency");};
      check(request(t,c,Operation::Emergency,0,NAN,at,broken) && disables==1 && reads==before,
            "Emergency halfway through wait shuts down without MADC or valid limit");
      check(!t.ramping && !t.step_wait && !t.step_sample_pending,"Emergency cancels all deferred work");
      for(unsigned ms=at+1000;ms<at+6000;ms+=1000)tick(t,c,20,ms,interruptible);
      check(commands.size()==1 && disables==1,"no deferred DAC after Emergency");
    }
    // Failed measurement or overcurrent after a DAC must never schedule a
    // normal next stage. Protection retains its original immediate descent.
    for(bool down : {false,true}) for(int failure=0;failure<3;++failure) {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;t.set=down?30:0;
      bool fail=false;std::vector<double> commands;
      Backend guarded{[&]()->Sample {
        if(fail && failure==0)throw std::runtime_error("MADC timeout");
        return Sample{t.set,fail && failure==1?20.001:20};
      },[&](double v){commands.push_back(v);},[]{},{},{}};
      check(request(t,c,down?Operation::Stop:Operation::Voltage,down?0:12,20,0,guarded),"guarded wait ramp");
      tick(t,c,20,1000,guarded);fail=true;tick(t,c,20,2000,guarded);
      if(failure==0) {
        tick(t,c,20,3000,guarded);
        check(t.faulted && t.result=="Indeterminate" && commands.size()==1 && !t.step_wait,"failed post-DAC measurement prevents next stage");
      } else if(failure==1) {
        check(t.emergency && !t.step_wait && commands.size()==2 && commands.back()<commands.front(),"overcurrent skips sleep and only permits protective descent");
      } else {
        check(t.step_wait,"safe current arms wait");
        tick(t,c,0,3000,guarded);
        check(t.faulted && commands.size()==1 && !t.step_wait,"invalid limit during wait prohibits deferred DAC");
      }
    }
    // Replay the former publication gap with the unchanged control freshness.
    {
      State t;std::uint64_t clock=800;
      Backend crossing{[&]{clock+=190;return Sample{0.1,1};},
        [&](double){clock+=20;},[]{},[&]{return clock;},{}};
      crossing.unix_time_ms=[&]{return 1000000+clock;};
      check(request(t,c,Operation::Enable,0,20,clock,crossing) && clock==1010 &&
            t.updated==990 && t.sampled_unix_ms==1000990,
            "sample timestamp stays before DAC even across a Unix second boundary");
    }
    for (bool down : {false,true}) {
      State t;t.enabled=t.enabled_known=t.set_known=t.valid=true;
      t.set=down?40:0;t.updated=1000;
      std::uint64_t clock=1000;unsigned measures=0,programs=0;
      Backend timed{[&]{++measures;clock+=190;return Sample{t.set,1};},
        [&](double){++programs;clock+=10;},[]{},[&]{return clock;},{}};
      timed.unix_time_ms=[&]{return 1000000+clock;};
      check(request(t,c,Operation::Voltage,down?10:12,20,clock,timed),"expiry replay request");
      for (auto ms : {2000u,3000u,4000u,5000u,6000u}) {
        clock=ms;tick(t,c,20,clock,timed);
      }
      check(t.ramping && !t.faulted && t.valid && fresh(t,clock) && t.updated==4190,
            "expiry replay has valid sample after second DAC");
      clock=7000;tick(t,c,20,clock,timed);
      check(t.valid && !fresh(t,clock) && measurementFresh(t,clock) && !t.faulted &&
            t.monitoring=="OK" && measures==2 && programs==2 && t.sampled_unix_ms==1004190,
            "normal post-DAC wait keeps display valid without changing control gate or sample time");
      check(!measurementFresh(t,8011),"stalled ramp stage must expire even within sample age cap");
      clock=8000;tick(t,c,20,clock,timed);
      check(fresh(t,clock) && t.updated==8190 && measures==3 && !t.faulted,
            "next successful MADC ends expiry gap");
      for (auto ms : {9000u,10000u,11000u,12000u,13000u,14000u,15000u,16000u}) {
        clock=ms;tick(t,c,20,clock,timed);
        check(measurementFresh(t,clock),"healthy ascent/descent display continuous through completion");
      }
      check(!t.ramping && t.ramp_status=="Steady" && t.result=="Succeeded", "ramp returns to steady monitoring");
      const auto completed_at=t.last_tick, measured_at=t.updated;
      const auto completed_measures=measures;
      clock=completed_at+29999;tick(t,c,20,clock,timed);
      check(measures==completed_measures && t.updated==measured_at && measurementFresh(t,clock),"steady keeps 30s cadence and original sample time");
      clock=completed_at+30000;tick(t,c,20,clock,timed);
      check(measures==completed_measures+1 && t.updated>measured_at,"steady measures at 30s");
      fault(t,"injected communication failure");
      check(!measurementFresh(t,clock),"fault immediately invalidates display");
    }
    State expiry;expiry.valid=true;expiry.updated=1000;
    check(fresh(expiry,66000) && !fresh(expiry,66001) && !fresh(expiry,999),"steady exact expiry and backward time");
    expiry.ramping=true;
    check(fresh(expiry,3000) && !fresh(expiry,3001),"ramp exact expiry");
    expiry.valid=false;check(!fresh(expiry,1000),"failed measurement cannot be fresh");
    expiry.valid=true;expiry.last_tick=7000;
    check(measurementFresh(expiry,7000) && !measurementFresh(expiry,7001),"display sample age cap cannot be extended by stage progress");
    expiry.ramping=false;
    check(measurementFresh(expiry,66000) && !measurementFresh(expiry,66001) && !measurementFresh(expiry,999),"display steady expiry and backward clock");
    check(!fresh(s,s.updated+65001),"stale data");
    std::cout<<"easiroc_hv_test: "<<checks<<" checks passed\n";
  } catch(const std::exception& e){std::cerr<<"easiroc_hv_test: "<<e.what()<<'\n';return 1;}
}
