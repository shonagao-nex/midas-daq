CXX ?= c++
MIDASSYS ?= /home/nagao/midas/midas_src
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -MMD -MP
CPPFLAGS += -I. -Iofficial -I$(MIDASSYS)/include
LDLIBS := $(MIDASSYS)/lib/libmidas.a -lm -lz -lutil -lpthread

BUILD := build
BIN := bin
CORE := $(BUILD)/hul_stream.o $(BUILD)/hul_snapshot.o
MIDAS_CORE := $(BUILD)/hul_bank.o
OFFICIAL := $(BUILD)/UDPRBCP.o $(BUILD)/FPGAModule.o
TESTS := $(BIN)/tests/hul_stream_test $(BIN)/tests/hul_snapshot_test \
	$(BIN)/tests/hul_rbcp_test $(BIN)/tests/hul_load_test \
	$(BIN)/tests/hul_run_guard_test
TESTS += $(BIN)/tests/hul_bank_test $(BIN)/tests/hul_monitor_test

.PHONY: all check clean
all: $(BIN)/fehul $(TESTS)

$(BIN)/fehul: $(BUILD)/fehul.o $(CORE) $(MIDAS_CORE) $(OFFICIAL) $(MIDASSYS)/lib/mfe.o
	@mkdir -p $(BIN)
	$(CXX) $^ -o $@ $(LDLIBS)

$(BIN)/tests/%: $(BUILD)/tests/%.o $(CORE) $(OFFICIAL)
	@mkdir -p $(BIN)/tests
	$(CXX) $^ -o $@ -lpthread

$(BIN)/tests/hul_bank_test: $(BUILD)/tests/hul_bank_test.o $(MIDAS_CORE) $(CORE)
	@mkdir -p $(BIN)/tests
	$(CXX) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD)/%.o: official/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

check: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit $$?; done

clean:
	rm -rf $(BUILD) $(BIN)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
