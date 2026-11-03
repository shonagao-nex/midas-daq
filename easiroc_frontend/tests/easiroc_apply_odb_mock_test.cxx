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
    check(explicit_depth==0,"final ODB lock leaked");
    std::cout<<"easiroc_apply_odb_mock_test: "<<checks<<" checks passed\n";
  } catch(const std::exception& error) {
    std::cerr<<"easiroc_apply_odb_mock_test: FAILED: "<<error.what()<<'\n';return 1;
  }
}
