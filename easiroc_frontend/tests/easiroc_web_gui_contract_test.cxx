#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int checks = 0;

void expect(bool condition, const char* message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

std::string readFile(const char* path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), {}};
}

}  // namespace

int main() {
  const std::string javascript = readFile("../daq_monitor/web/easiroc.js");
  const std::string html = readFile("../daq_monitor/web/easiroc.html");
  const std::string clear_js =
      readFile("../daq_monitor/web/buffer-clear.js");
  const std::string clear_html =
      readFile("../daq_monitor/web/buffer-clear.html");
  expect(!javascript.empty(), "cannot read EASIROC WebGUI JavaScript");
  expect(!html.empty(), "cannot read EASIROC WebGUI HTML");
  expect(!clear_js.empty() && !clear_html.empty(),
         "cannot read buffer-clear WebGUI");
  for (const char* field : {"HGShapingTime", "LGShapingTime",
                            "ChannelEnabled"}) {
    expect(javascript.find(field) != std::string::npos,
           "WebGUI does not include shaping ODB/readback field");
  }
  expect(javascript.find("sameSettings") != std::string::npos &&
             javascript.find("hgShaping === right[name].hgShaping") !=
                 std::string::npos &&
             javascript.find("lgShaping === right[name].lgShaping") !=
                 std::string::npos,
         "WebGUI unsaved comparison omits shaping time");
  expect(javascript.find("const values = [state.staged.asic1.code") !=
                 std::string::npos &&
             javascript.find("state.staged.asic1.hgShaping") !=
                 std::string::npos &&
             javascript.find("ODB readback did not match all saved Settings") !=
                 std::string::npos,
         "WebGUI save/readback omits shaping time");
  expect(javascript.find("legacyShaping(state.loaded)") ==
                 std::string::npos &&
             javascript.find("does not yet support non-legacy Shaping Time") ==
                 std::string::npos &&
             javascript.find("isUnsaved || !statusAvailable()") !=
                 std::string::npos,
         "WebGUI still suppresses valid non-legacy shaping apply");
  expect(html.find("asic1-hg-shaping") != std::string::npos &&
             html.find("asic2-lg-shaping") != std::string::npos,
         "WebGUI shaping selects are missing");
  expect(html.find("asic1-channel-mask") != std::string::npos &&
             html.find("asic2-channel-mask") != std::string::npos &&
             javascript.find("channelEnabled.every") != std::string::npos,
         "WebGUI channel mask grid or unsaved comparison is missing");
  expect(javascript.find("state.staged.asic1.channelEnabled") !=
                 std::string::npos &&
             javascript.find("state.staged.asic2.channelEnabled") !=
                 std::string::npos &&
             javascript.find("ODB.asic1ChannelEnabled") !=
                 std::string::npos &&
             javascript.find("ODB.asic2ChannelEnabled") !=
                 std::string::npos,
         "WebGUI ChannelEnabled save/readback is missing");
  expect(javascript.find("legacyChannelMask(state.loaded)") ==
                 std::string::npos &&
             javascript.find("does not yet support masked channels") ==
                 std::string::npos &&
             javascript.find("saved valid Settings") != std::string::npos,
         "WebGUI still suppresses Apply for a valid non-default channel mask");
  expect(clear_js.find("mjsonrpc_cm_exist(\"fevme\",true)") !=
                 std::string::npos &&
             clear_js.find("mjsonrpc_cm_exist(\"feeasiroc\",true)") !=
                 std::string::npos,
         "buffer-clear WebGUI does not check exact frontend clients");
  expect(clear_js.find("Number(state.values[PATHS.run])!==STOPPED") !=
                 std::string::npos &&
             clear_js.find("BufferClearRequestId") != std::string::npos,
         "buffer-clear WebGUI lacks STOPPED check or request mailbox");
  expect(clear_html.find("Clear VME event buffers") != std::string::npos &&
             clear_html.find("Clear EASIROC receive/event buffer") !=
                 std::string::npos,
         "buffer-clear controls are missing");
  std::cout << "easiroc_web_gui_contract_test: " << checks
            << " checks passed\n";
}
