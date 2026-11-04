// Compiles the real ODB adapter against an in-memory MIDAS API, without libmidas
// or transport code. No experiment connection, sockets, or shared memory.
#include "easiroc_odb.h"
#include "msystem.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <type_traits>

HNDLE hDB = 1;
namespace {
struct Value { DWORD type; INT count; std::vector<unsigned char> data; };
std::map<std::string, Value> values;
std::map<std::string, HNDLE> handles;
std::map<HNDLE, std::string> paths;
std::recursive_mutex odb_mutex;
int explicit_depth = 0, checks = 0, hv_reads = 0;
std::string reject_write_path;
void check(bool condition, const char* reason) {
  ++checks;
  if (!condition) throw std::runtime_error(reason);
}
void directory(const std::string& path) {
  if (handles.count(path)) return;
  if (path != "/") directory(path.substr(0, path.rfind('/')).empty() ? "/" : path.substr(0,path.rfind('/')));
  const HNDLE handle = handles.size()+1;
  handles[path] = handle; paths[handle] = path;
  values[path] = {TID_KEY,0,{}};
}
std::string absolute(HNDLE root, const char* name) {
  if (name[0] == '/') return name;
  return (root ? paths.at(root) : "") + "/" + name;
}
template <typename T> void put(const std::string& path, T value, DWORD type) {
  db_set_value(hDB,0,path.c_str(),&value,sizeof(value),1,type);
}
void text(const std::string& path, const std::string& value) {
  db_set_value(hDB,0,path.c_str(),value.c_str(),value.size()+1,1,TID_STRING);
}
template <typename T> void setting(const std::string& path, T& value) {
  if constexpr (std::is_same_v<T,int>) put(path,value,TID_INT);
  else if constexpr (std::is_same_v<T,bool>) put(path,BOOL(value),TID_BOOL);
  else {
    using Element = typename T::value_type;
    using Wire = std::conditional_t<std::is_same_v<Element,bool>,BOOL,INT>;
    std::array<Wire,std::tuple_size<T>::value> wire{};
    for (unsigned i=0;i<wire.size();++i) wire[i]=value[i];
    db_set_value(hDB,0,path.c_str(),wire.data(),sizeof(wire),wire.size(),
        std::is_same_v<Element,bool>?TID_BOOL:TID_INT);
  }
}
int number(const std::string& path) {
  const auto& value=values.at(path);
  int result=0; std::memcpy(&result,value.data.data(),sizeof(result)); return result;
}
std::string stringValue(const std::string& path) {
  return reinterpret_cast<const char*>(values.at(path).data.data());
}
}

INT db_lock_database(HNDLE) { odb_mutex.lock(); ++explicit_depth; return DB_SUCCESS; }
INT db_unlock_database(HNDLE) { --explicit_depth; odb_mutex.unlock(); return DB_SUCCESS; }
bool rpc_is_remote() { return false; }
INT cm_msg(INT,const char*,INT,const char*,const char*,...) { return CM_SUCCESS; }
INT db_find_key(HNDLE,HNDLE root,const char* name,HNDLE* key) {
  std::lock_guard<std::recursive_mutex> lock(odb_mutex);
  const auto path=absolute(root,name);
  const auto found=handles.find(path);
  if(found==handles.end()) return DB_NO_KEY;
  *key=found->second; return DB_SUCCESS;
}
INT db_get_key(HNDLE,HNDLE handle,KEY* key) {
  std::lock_guard<std::recursive_mutex> lock(odb_mutex);
  const auto& path=paths.at(handle); const auto& value=values.at(path);
  *key={};key->type=value.type;key->num_values=value.count;
  std::snprintf(key->name,sizeof(key->name),"%s",path.substr(path.rfind('/')+1).c_str());
  return DB_SUCCESS;
}
INT db_enum_key(HNDLE,HNDLE root,INT index,HNDLE* child) {
  std::lock_guard<std::recursive_mutex> lock(odb_mutex);
  int found=0; const auto prefix=paths.at(root)+"/";
  for(const auto& entry:handles) {
    if(entry.first.rfind(prefix,0)!=0 || entry.first.find('/',prefix.size())!=std::string::npos) continue;
    if(found++==index) { *child=entry.second; return DB_SUCCESS; }
  }
  return DB_NO_MORE_SUBKEYS;
}
INT db_get_value(HNDLE,HNDLE root,const char* name,void* data,INT* size,DWORD type,BOOL create) {
  std::lock_guard<std::recursive_mutex> lock(odb_mutex);
  check(!create,"ODB read tried to create a key");
  const auto path=absolute(root,name);
  if(path.find("/HV/")!=std::string::npos) ++hv_reads;
  const auto found=values.find(path);
  if(found==values.end()) return DB_NO_KEY;
  if(found->second.type!=type) return DB_TYPE_MISMATCH;
  if(*size<static_cast<int>(found->second.data.size())) return DB_TRUNCATED;
  *size=found->second.data.size();std::memcpy(data,found->second.data.data(),*size);
  return DB_SUCCESS;
}
INT db_set_value(HNDLE,HNDLE root,const char* name,const void* data,INT size,INT count,DWORD type) {
  std::lock_guard<std::recursive_mutex> lock(odb_mutex);
  const auto path=absolute(root,name);
  if (path == reject_write_path) return DB_NO_ACCESS;
  directory(path.substr(0,path.rfind('/')));
  if(!handles.count(path)) {const HNDLE handle=handles.size()+1;handles[path]=handle;paths[handle]=path;}
  const auto* bytes=static_cast<const unsigned char*>(data);
  values[path]={type,count,{bytes,bytes+size}}; return DB_SUCCESS;
}

int main() {
  try {
    const std::string base="/Equipment/EASIROC/Devices/moduleA";
    const auto status=base+"/Variables/ASICSlowControl/";
    const auto command=base+"/Commands/ASICSlowControl/ApplyRequestId";
    text("/Equipment/EASIROC/Settings/Network/IPAddress","192.168.10.39");
    text(base+"/IPAddress","192.168.10.40");
    put("/Runinfo/State",INT(STATE_STOPPED),TID_INT);
    put("/Runinfo/Transition in progress",INT(0),TID_INT);
    put(base+"/Settings/Enabled",BOOL(TRUE),TID_BOOL);
    easiroc::AsicSlowControlSettings desired;desired.extras=easiroc::SlowControlExtras{};
    desired.extras->probe_enabled={true,true};desired.extras->and_channels[63]=true;
    easiroc::visitSlowControlSettings(desired,[&](const auto& path,auto& value){setting(base+"/Settings/"+path,value);return true;});
    put(command,DWORD(0),TID_DWORD);
    put(status+"LastHandledRequestId",DWORD(0),TID_DWORD);
    put(status+"LastSuccessfulRequestId",DWORD(0),TID_DWORD);
    put(status+"ApplyInProgress",BOOL(FALSE),TID_BOOL);
    put(status+"HardwareStateIndeterminate",BOOL(FALSE),TID_BOOL);
    put(status+"LastApplied/Valid",BOOL(FALSE),TID_BOOL);
    int writes=0,fail_at=0;bool fail_rollback=false;
    std::vector<std::uint32_t> addresses;
    auto writer=[&](const std::string& ip,std::uint32_t address,const std::vector<std::uint8_t>&) {
      check(explicit_depth>0,"hardware write outside exclusive ODB scope");
      check(ip=="192.168.10.40","wrong target IP");
      ++writes;addresses.push_back(address);
      if(fail_at && (writes==fail_at || (fail_rollback && writes>fail_at))) throw std::runtime_error("mock write failure");
    };
    auto delay=[&](unsigned ms){check(ms==100 && explicit_depth>0,"delay outside locked plan");};
    auto poll=[&](){easiroc_odb::process_device_apply_requests(100,writer,delay);check(explicit_depth==0,"ODB lock leaked");};
    poll(); // first observation establishes the restart boundary
    check(writes==0,"Save/Settings change caused hardware access");
    DWORD request=0;
    auto apply=[&](){writes=0;addresses.clear();put(command,++request,TID_DWORD);poll();};
    for(const char* hv:{"ON","OFF","Unknown","NotImplemented"}) {
      if (std::string(hv)=="NotImplemented") values.erase(base+"/HV/State");
      else text(base+"/HV/State",hv);
      apply();
      check(writes==14 && number(status+"LastAttemptSucceeded")==TRUE,"HV blocked STOPPED Apply");
      check(number(status+"LastApplied/TriggerWidth")==118,"extended LastApplied omitted width");
      check(addresses[12]==0x78 && addresses[13]==0x88,"full plan omitted logic/width");
    }
    check(hv_reads==0,"Apply consulted HV state");
    for(int run:{STATE_RUNNING,STATE_PAUSED,0}) {
      put("/Runinfo/State",INT(run),TID_INT);apply();
      check(writes==0 && stringValue(status+"ApplyState")=="Rejected","active/unknown DAQ permitted Apply");
    }
    put("/Runinfo/State",INT(STATE_STOPPED),TID_INT);
    put("/Runinfo/Transition in progress",INT(TR_START),TID_INT);apply();
    check(writes==0,"START transition permitted Apply");
    values.erase("/Runinfo/Transition in progress");apply();
    check(writes==0,"unknown transition permitted Apply");
    put("/Runinfo/Transition in progress",INT(0),TID_INT);
    const auto applied_id=number(status+"LastApplied/RequestId");
    put(base+"/Settings/SelectableLogic/OneChannel",INT(64),TID_INT);apply();
    check(writes==0,"invalid snapshot caused hardware access");
    put(base+"/Settings/SelectableLogic/OneChannel",INT(63),TID_INT);
    fail_at=3;apply();
    check(writes==17 && number(status+"RollbackSucceeded")==TRUE &&
          number(status+"HardwareStateIndeterminate")==FALSE,"single rollback did not restore complete settings");
    check(number(status+"LastApplied/RequestId")==applied_id &&
          number(base+"/Settings/SelectableLogic/OneChannel")==63,"failed Apply changed desired/history");
    fail_rollback=true;apply();
    check(writes==4 && number(status+"RollbackAttempted")==TRUE &&
          number(status+"RollbackSucceeded")==FALSE && number(status+"HardwareStateIndeterminate")==TRUE,
          "rollback failure was not recorded");
    check(!easiroc_odb::device_start_inhibit_reason().empty(),"rollback failure permitted DAQ START");
    fail_at=0;fail_rollback=false;apply();
    check(easiroc_odb::device_start_inhibit_reason().empty(),"successful re-apply did not clear START inhibit");

    // ODB publication failure must not erase the in-process START inhibit.
    reject_write_path=status+"HardwareStateIndeterminate";
    fail_at=3;fail_rollback=true;apply();
    check(number(status+"HardwareStateIndeterminate")==FALSE &&
          !easiroc_odb::device_start_inhibit_reason().empty(),
          "failed status persistence allowed START despite failed rollback");
    reject_write_path.clear();fail_at=0;fail_rollback=false;
    poll(); // acknowledgement-only recovery of the unpersisted request
    apply();
    check(easiroc_odb::device_start_inhibit_reason().empty(),"persisted recovery did not clear memory inhibit");

    // A competing START's real ODB flag write must wait until all Apply writes,
    // LastApplied and terminal status have been published and the lock released.
    std::future<void> start;
    bool first=true;
    std::promise<void> attempted;
    auto attempted_future=attempted.get_future();
    put(command,++request,TID_DWORD);writes=0;
    easiroc_odb::process_device_apply_requests(200,
      [&](const auto& ip,auto address,const auto& data) {
        writer(ip,address,data);
        if(first) {
          first=false;
          start=std::async(std::launch::async,[&](){attempted.set_value();put("/Runinfo/Transition in progress",INT(TR_START),TID_INT);});
          attempted_future.wait();
          check(start.wait_for(std::chrono::milliseconds(10))==std::future_status::timeout,"START bypassed Apply exclusion");
        }
      },delay);
    start.get();
    check(writes==14 && number(status+"LastHandledRequestId")==static_cast<int>(request) &&
          number("/Runinfo/Transition in progress")==TR_START,"START/Apply serialization failed");
    apply();check(writes==0,"request after competing START was executed");
    // Exercise the implicit legacy device's optional full Settings and
    // LastApplied codec, including compatibility with an ASIC-only ODB.
    check(easiroc_odb::initialize_acquisition_settings_odb() &&
          easiroc_odb::initialize_asic_settings_odb() && easiroc_odb::initialize_last_applied_odb(),
          "mock legacy schema initialization failed");
    easiroc_frontend::FrontendSettings legacy;
    check(easiroc_odb::read_settings(&legacy)==SUCCESS && !legacy.asic_slow_control.extras,
          "legacy ASIC-only schema stopped working");
    easiroc::visitSlowControlSettings(desired,[&](const auto& path,auto& value){
      setting(std::string("/Equipment/EASIROC/Settings/")+path,value);return true;
    });
    check(easiroc_odb::read_settings(&legacy)==SUCCESS && legacy.asic_slow_control.extras==desired.extras,
          "legacy full Settings omitted extras");
    easiroc::AppliedAsicSlowControlSettings legacy_applied;
    legacy_applied.valid=true;legacy_applied.request_id=42;
    legacy_applied.asic=desired.asic;legacy_applied.extras=desired.extras;
    check(easiroc_odb::publish_last_applied_settings(legacy_applied),"legacy extended LastApplied publication failed");
    easiroc::AppliedAsicSlowControlSettings restored;
    bool indeterminate=true;
    check(easiroc_odb::read_last_applied_settings(&restored,&indeterminate) &&
          !indeterminate && restored.extras==desired.extras && restored.request_id==42,
          "legacy extended LastApplied roundtrip failed");
    values.erase("/Equipment/EASIROC/Settings/Probe/ASIC2/Channel");
    check(easiroc_odb::read_settings(&legacy)!=SUCCESS,"partial extended legacy schema accepted");
    // Real HV adapter, in-memory MIDAS and injected transports only.
    text("/Equipment/EASIROC/Settings/Network/IPAddress","192.168.10.40");
    text(base+"/IPAddress","192.168.10.41");
    put("/Runinfo/State",INT(STATE_STOPPED),TID_INT);
    put("/Runinfo/Transition in progress",INT(0),TID_INT);
    for(const auto& route:{std::string("/Equipment/EASIROC"),base}) {
      const auto av=route+"/Variables/ASICSlowControl/";
      put(route+"/Settings/Enabled",BOOL(TRUE),TID_BOOL);
      put(av+"ApplyInProgress",BOOL(FALSE),TID_BOOL);
      put(av+"HardwareStateIndeterminate",BOOL(FALSE),TID_BOOL);
      put(av+"LastApplied/Valid",BOOL(TRUE),TID_BOOL);
      put(route+"/Commands/ASICSlowControl/ApplyRequestId",DWORD(50),TID_DWORD);
      put(av+"LastHandledRequestId",DWORD(50),TID_DWORD);
      std::array<int,32> input{};input.fill(480);
      setting(av+"LastApplied/ASIC1/InputDAC",input);setting(av+"LastApplied/ASIC2/InputDAC",input);
    }
    int hv_io=0;std::map<std::string,double> output{{"192.168.10.40",0.4},{"192.168.10.41",0.6}};
    std::vector<std::string> hv_writes;
    bool freeze_hv_output=false;
    easiroc_odb::HvBackendFactory hv_backend=[&](const std::string& ip) {
      easiroc::hv::Backend io;
      io.measure=[&,ip] {check(explicit_depth>0,"HV I/O outside existing exclusion");++hv_io;return easiroc::hv::Sample{output.at(ip),1};};
      io.set=[&,ip](double value) {++hv_io;hv_writes.push_back(ip+" DAC");if(!freeze_hv_output)output.at(ip)=value;};
      io.shutdown=[&,ip] {++hv_io;hv_writes.push_back(ip+" shutdown");};
      return io;
    };
    auto service=[&](std::uint64_t ms,bool verified=true){easiroc_odb::process_hv_requests(600+ms/1000,ms,hv_backend,verified);};
    const auto hv=base+"/Variables/HV/", hc=base+"/Commands/HV/";
    auto hv_request=[&](DWORD id,const std::string& operation,double target) {
      text(hc+"RequestPayload",std::to_string(id)+" "+operation+" "+std::to_string(target));
      put(hc+"RequestId",id,TID_DWORD);
    };
    easiroc_odb::reset_hv_runtime();service(0);
    check(hv_io==0 && stringValue(hv+"EnabledState")=="Unknown" && number(hv+"MeasurementValid")==0,"startup must not infer or contact hardware");
    double retained=0;INT retained_size=sizeof(retained);
    check(db_get_value(hDB,0,(base+"/Settings/HV/CurrentLimit_uA").c_str(),&retained,&retained_size,TID_DOUBLE,FALSE)==DB_SUCCESS && retained==20,"default CurrentLimit must be 20 uA");
    put(base+"/Settings/HV/CurrentLimit_uA",10.,TID_DOUBLE);
    service(0);retained_size=sizeof(retained);
    check(db_get_value(hDB,0,(base+"/Settings/HV/CurrentLimit_uA").c_str(),&retained,&retained_size,TID_DOUBLE,FALSE)==DB_SUCCESS && retained==10,
          "existing ODB CurrentLimit must not be overwritten by new default");
    const auto prior_apply=number(command);
    check(number(hv+"EnableAvailable")==0 && number(base+"/Settings/HV/HardwareAccessEnabled")==0,
          "new device must inhibit HV hardware access");
    service(1000);check(hv_io==0,"inhibited periodic service contacted hardware");
    hv_request(1,"Enable",0);service(2000);
    check(hv_io==0 && stringValue(hv+"Result")=="Rejected" && number(hv+"LastHandledRequestId")==1,
          "inhibited command must fail without I/O, never simulation success");
    for(const auto& route:{std::string("/Equipment/EASIROC"),base})
      put(route+"/Settings/HV/HardwareAccessEnabled",BOOL(TRUE),TID_BOOL);
    // A rejected request must not replay when hardware permission is granted.
    service(2001);check(hv_io==0,"permission change replayed rejected request");
    easiroc_odb::reset_hv_runtime();put(hc+"RequestId",DWORD(0),TID_DWORD);put(hv+"LastHandledRequestId",DWORD(0),TID_DWORD);service(0);
    hv_request(1,"Emergency",0);service(1000);
    check(stringValue(hv+"EnabledState")=="OFF" && hv_writes.back()=="192.168.10.41 shutdown","emergency IP routing");
    check(stringValue("/Equipment/EASIROC/Variables/HV/EnabledState")=="Unknown","HV state crossed device boundary");
    hv_request(2,"Enable",0);service(2000,false);
    check(stringValue(hv+"Result")=="Rejected" && number(hv+"EnableAvailable")==0,"unverified protocol gate bypassed");
    hv_request(3,"Enable",0);service(3000);service(4000);service(5000);
    check(stringValue(hv+"EnabledState")=="ON" && stringValue(hv+"Result")=="Succeeded" &&
          number(hv+"LastHandledRequestId")==3,"Enable from unknown retained DAC/final measurements");
    hv_request(4,"Voltage",62);service(6000);service(7000);
    check(output.at("192.168.10.41")==4 && output.at("192.168.10.40")==0.4,"ramp IP isolation");
    put(base+"/Settings/HV/CurrentLimit_uA",12.5,TID_DOUBLE);
    const auto before_wait=hv_writes.size();service(8000);
    check(hv_writes.size()==before_wait && output.at("192.168.10.41")==4 &&
          number(hv+"InProgress")==1 && number(hv+"EmergencyAvailable")==1,
          "post-measurement wait must preserve Emergency mailbox capability without next DAC");
    hv_request(5,"Emergency",0);service(8500);
    check(hv_writes.size()==before_wait+1 && hv_writes.back()=="192.168.10.41 shutdown" &&
          number(hv+"InProgress")==0,"Emergency mailbox executes halfway through post-measurement wait");
    retained_size=sizeof(retained);
    check(db_get_value(hDB,0,(base+"/Settings/HV/CurrentLimit_uA").c_str(),&retained,&retained_size,TID_DOUBLE,FALSE)==DB_SUCCESS && retained==12.5 && number(command)==prior_apply,"HV changed independent CurrentLimit/ASIC request");
    const int no_replay=hv_io;service(9000);
    check(hv_io==no_replay,"same HV request replayed");
    hv_request(6,"Emergency",0);easiroc_odb::reset_hv_runtime();service(10000);
    check(hv_io==no_replay && stringValue(hv+"EnabledState")=="Unknown" && stringValue(hv+"Result")=="Indeterminate" && number(hv+"LastHandledRequestId")==6,"restart replayed a command or inferred state");
    text(base+"/IPAddress","192.168.10.40");hv_request(7,"Emergency",0);service(11000);
    check(hv_io==no_replay && stringValue(hv+"BlockedReason").find("registry")!=std::string::npos,"duplicate IP not rejected");
    text(base+"/IPAddress","192.168.10.41");service(12000);
    for(const auto state:{STATE_RUNNING,STATE_PAUSED}) {
      put("/Runinfo/State",INT(state),TID_INT);hv_request(state+10,"Emergency",0);service(13000+state*1000);
      check(hv_io==no_replay && stringValue(hv+"Result")=="Rejected","RUN/PAUSED caused HV traffic");
    }
    put("/Runinfo/State",INT(STATE_STOPPED),TID_INT);
    put("/Runinfo/Transition in progress",INT(TR_START),TID_INT);hv_request(20,"Emergency",0);service(18000);
    check(hv_io==no_replay,"transition caused HV traffic");
    put("/Runinfo/Transition in progress",INT(0),TID_INT);
    put(base+"/Variables/ASICSlowControl/ApplyInProgress",BOOL(TRUE),TID_BOOL);hv_request(21,"Emergency",0);service(19000);
    check(hv_io==no_replay,"ASIC Apply overlapped HV");
    put(base+"/Variables/ASICSlowControl/ApplyInProgress",BOOL(FALSE),TID_BOOL);
    // Reject an envelope replaced by another browser before RequestId commit.
    hv_request(22,"Emergency",0);text(hc+"RequestPayload","23 Voltage 62");service(20000);
    check(hv_io==no_replay && stringValue(hv+"Error").find("envelope")!=std::string::npos,"torn request executed");
    reject_write_path=hv+"Error";hv_request(23,"Emergency",0);service(21000);
    check(number(hv+"LastHandledRequestId")==22,"HV acknowledged failed publication");
    const int writes_after_failure=hv_io;reject_write_path.clear();service(22000);
    check(hv_io==writes_after_failure && stringValue(hv+"EnabledState")=="Unknown","failed publication replayed hardware");
    // Fresh startup is eligible without manufacturing DAC/measurement state.
    easiroc_odb::reset_hv_runtime();service(23000);output.at("192.168.10.41")=0.4;
    check(number(hv+"EnableAvailable")==1 && number(hv+"MeasurementValid")==0 &&
          stringValue(hv+"EnabledState")=="Unknown" && stringValue(hv+"SetVoltageState")=="Unknown",
          "startup capability must accept Unknown without promoting it to measured state");
    const auto before_initial_enable=hv_writes.size();
    hv_request(24,"Enable",0);service(24000);service(25000);service(26000);
    check(hv_writes.size()==before_initial_enable+1 && hv_writes.back()=="192.168.10.41 DAC" &&
          stringValue(hv+"Result")=="Succeeded" && number(hv+"LastHandledRequestId")==24,
          "initial Enable must not require or inject shutdown");
    // Pending commands are still discarded on restart; only a new ID executes.
    hv_request(25,"Enable",0);easiroc_odb::reset_hv_runtime();const int before_restart=hv_io;service(27000);
    check(hv_io==before_restart && number(hv+"LastHandledRequestId")==25 &&
          stringValue(hv+"Result")=="Indeterminate" && stringValue(hv+"EnabledState")=="Unknown",
          "compatibility change replayed old request or inferred post-restart state");
    hv_request(26,"Enable",0);service(28000);service(29000);service(30000);
    check(stringValue(hv+"Result")=="Succeeded" && number(hv+"LastHandledRequestId")==26 && number(command)==prior_apply,
          "new post-restart Enable failed or changed ASIC mailbox");
    const auto result_time_before=number(hv+"ResultUnixTime");
    const auto result_id_before=number(hv+"ResultRequestId");
    const int before_inhibit=hv_io;
    for(const auto& route:{std::string("/Equipment/EASIROC"),base})
      put(route+"/Settings/HV/HardwareAccessEnabled",BOOL(FALSE),TID_BOOL);
    service(31000);
    check(hv_io==before_inhibit && stringValue(hv+"EnabledState")=="Unknown" && number(hv+"MeasurementValid")==0 &&
          number(hv+"EnableAvailable")==0 && number(hv+"EmergencyAvailable")==0,
          "revoked hardware permission must invalidate state and suppress all I/O");
    check(stringValue(hv+"Result")=="Succeeded" && stringValue(hv+"Error").empty() &&
          number(hv+"ResultUnixTime")==result_time_before && number(hv+"ResultRequestId")==result_id_before &&
          stringValue(hv+"MonitoringStatus").find("revoked")!=std::string::npos,
          "permission revoke must preserve completed result and publish monitoring status separately");
    hv_request(27,"Emergency",0);service(62000);
    check(hv_io==before_inhibit && stringValue(hv+"Result")=="Rejected" && number(command)==prior_apply,
          "inhibited emergency/monitoring must not execute or alter ASIC Apply");
    // Exercise a real adapter mailbox through a long, physically untracked
    // Stop, then supersede it with Emergency and verify no replay.
    put(base+"/Settings/HV/HardwareAccessEnabled",BOOL(TRUE),TID_BOOL);
    hv_request(28,"Emergency",0);service(63000);
    output.at("192.168.10.41")=0.4;
    hv_request(29,"Enable",0);service(64000);service(65000);
    hv_request(30,"Voltage",10);service(66000);
    for(std::uint64_t ms=67000;ms<=72000;ms+=1000)service(ms);
    check(output.at("192.168.10.41")==10 && stringValue(hv+"Result")=="Succeeded","mock upward ramp before Stop");
    freeze_hv_output=true;
    hv_request(31,"Stop",0);service(73000);service(74000);
    const auto stopped_dac_count=hv_writes.size();
    const auto last_before_wait=number(hv+"LastUpdateUnixTime");
    for(std::uint64_t ms=75000;ms<=205000;ms+=1000)service(ms);
    check(hv_writes.size()==stopped_dac_count && stringValue(hv+"Result")=="Pending" &&
          number(hv+"ActiveRequestId")==31 && number(hv+"LastHandledRequestId")==30 &&
          number(hv+"MeasurementValid")==1 && number(hv+"LastUpdateUnixTime")>last_before_wait &&
          number(hv+"SetVoltage_V")==0 && stringValue(hv+"MonitoringStatus")=="OK" &&
          number(hv+"EmergencyAvailable")==1,"Stop must remain pending with live measurement and Emergency after 120 seconds");
    hv_request(32,"Emergency",0);service(206000);
    check(stringValue(hv+"Result")=="Succeeded" && stringValue(hv+"EnabledState")=="OFF" &&
          number(hv+"ActiveRequestId")==0 && number(hv+"LastHandledRequestId")==32 &&
          number(hv+"ResultRequestId")==32 && hv_writes.back()=="192.168.10.41 shutdown",
          "Emergency must supersede pending Stop and acknowledge its own request");
    const auto after_emergency=hv_writes.size();
    output.at("192.168.10.41")=0.5;
    for(std::uint64_t ms=207000;ms<=280000;ms+=1000)service(ms);
    check(hv_writes.size()==after_emergency && number(hv+"ResultRequestId")==32 && number(command)==prior_apply,
          "old Stop must not resume after Emergency or mutate ASIC request");
    put(base+"/Settings/HV/HardwareAccessEnabled",BOOL(FALSE),TID_BOOL);service(281000);
    check(stringValue(hv+"Result")=="Succeeded" && number(hv+"ResultRequestId")==32 &&
          number(hv+"MeasurementValid")==0,"Emergency success must survive monitor inhibition");
    // Reproduce the expiry gap through the real publisher, with no transport.
    put(base+"/Settings/HV/HardwareAccessEnabled",BOOL(TRUE),TID_BOOL);
    easiroc_odb::reset_hv_runtime();freeze_hv_output=false;
    output.at("192.168.10.41")=0.4;
    std::uint64_t clock=300000;bool fail_measure=false;
    auto timed_backend=[&](const std::string& ip) {
      auto backend=hv_backend(ip);
      const auto measure=backend.measure;const auto set=backend.set;
      backend.measure=[&,measure] {
        if(fail_measure)throw std::runtime_error("injected MADC timeout");
        clock+=190;return measure();
      };
      backend.set=[&,set](double value){clock+=10;set(value);};
      backend.now=[&]{return clock;};backend.unix_time=[&]{return 600+clock/1000;};
      backend.unix_time_ms=[&]{return 600000+clock;};
      return backend;
    };
    auto timed_service=[&](std::uint64_t ms) {
      clock=ms;easiroc_odb::process_hv_requests(600+ms/1000,ms,timed_backend,true);
    };
    timed_service(300000);hv_request(33,"Enable",0);timed_service(301000);
    check(number(hv+"LastUpdateUnixTimeMs")==901190,"published time is sample completion before Enable DAC");
    timed_service(302000);timed_service(303000);
    hv_request(34,"Voltage",12);timed_service(304000);
    for(auto ms:{305000u,306000u,307000u,308000u,309000u})timed_service(ms);
    check(number(hv+"MeasurementValid")==1 && stringValue(hv+"MonitoringStatus")=="OK",
          "timed publisher shows successful sample after DAC");
    const auto sample_time=number(hv+"LastUpdateUnixTime");
    const auto sample_time_ms=number(hv+"LastUpdateUnixTimeMs");
    const auto before_expiry_io=hv_io;
    timed_service(310000);
    check(number(hv+"MeasurementValid")==1 && number(hv+"LastUpdateUnixTime")==sample_time &&
          number(hv+"LastUpdateUnixTimeMs")==sample_time_ms && sample_time_ms==907190 &&
          stringValue(hv+"MonitoringStatus")=="OK" && stringValue(hv+"Result")=="Pending" && hv_io==before_expiry_io,
          "normal wait remains valid without communication or refreshing the sample timestamp");
    timed_service(311000);
    check(number(hv+"MeasurementValid")==1 && number(hv+"LastUpdateUnixTime")>sample_time,
          "next sample restores publisher validity");
    const auto before_failure_time=number(hv+"LastUpdateUnixTimeMs");
    fail_measure=true;
    // Fault is injected at the next actual sample, after the deferred DAC.
    for(auto ms:{312000u,313000u,314000u,315000u})timed_service(ms);
    check(number(hv+"MeasurementValid")==0 && stringValue(hv+"MonitoringStatus")=="injected MADC timeout" &&
          stringValue(hv+"Result")=="Indeterminate" && number(hv+"LastUpdateUnixTimeMs")==before_failure_time,
          "measurement failure invalidates without refreshing timestamp");
    hv_request(35,"Emergency",0);timed_service(316000);
    // Existing service immediately attempts residual-voltage monitoring after
    // shutdown; the injected MADC failure remains active for that attempt.
    check(number(hv+"MeasurementValid")==0 && stringValue(hv+"Result")=="Succeeded" &&
          stringValue(hv+"MonitoringStatus")=="injected MADC timeout" && hv_writes.back()=="192.168.10.41 shutdown" &&
          number(hv+"LastUpdateUnixTimeMs")==before_failure_time,
          "Emergency executes despite MADC fault; failed residual monitoring cannot manufacture a fresh sample");
    check(explicit_depth==0,"final ODB lock leaked");
    std::cout<<"easiroc_apply_odb_mock_test: "<<checks<<" checks passed\n";
  } catch(const std::exception& error) {
    std::cerr<<"easiroc_apply_odb_mock_test: FAILED: "<<error.what()<<'\n';return 1;
  }
}
