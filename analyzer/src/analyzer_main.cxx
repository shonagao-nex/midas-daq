#include "EventInspector.h"
#include "manalyzer.h"

namespace {
TARegister register_event_inspector(
    new TAFactoryTemplate<ana::EventInspector>);
}  // namespace

int main(int argc, char* argv[]) { return manalyzer_main(argc, argv); }

