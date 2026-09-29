CXX ?= g++
CPPFLAGS ?=
CPPFLAGS += -I$(MIDASSYS)/include -MMD -MP
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

MIDASSYS ?= /home/daq/midas/midas_src
MIDASLIB := $(MIDASSYS)/lib/libmidas.a
LIBS := -lsqlite3 -lm -lz -lutil -lpthread

BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/daq_monitor
TEST_TARGET := $(BIN_DIR)/tests/status_policy_test
OBJECTS := $(BUILD_DIR)/daq_monitor.o $(BUILD_DIR)/status_policy.o \
	$(BUILD_DIR)/alarm_policy.o $(BUILD_DIR)/monitor_alarms.o \
	$(BUILD_DIR)/runlog_edit.o \
	$(BUILD_DIR)/runlog_edit_rpc.o
TEST_OBJECTS := $(BUILD_DIR)/tests/status_policy_test.o $(BUILD_DIR)/status_policy.o
ALARM_TEST_OBJECTS := $(BUILD_DIR)/tests/alarm_policy_test.o \
	$(BUILD_DIR)/alarm_policy.o $(BUILD_DIR)/status_policy.o
RUNLOG_EDIT_TEST_OBJECTS := $(BUILD_DIR)/tests/runlog_edit_test.o \
	$(BUILD_DIR)/runlog_edit.o
RUNLOG_EDIT_RPC_TEST_OBJECTS := $(BUILD_DIR)/tests/runlog_edit_rpc_test.o \
	$(BUILD_DIR)/runlog_edit.o $(BUILD_DIR)/runlog_edit_rpc.o
TEST_TARGETS := $(TEST_TARGET) $(BIN_DIR)/tests/alarm_policy_test \
	$(BIN_DIR)/tests/runlog_edit_test \
	$(BIN_DIR)/tests/runlog_edit_rpc_test
DEPS := $(OBJECTS:.o=.d) $(TEST_OBJECTS:.o=.d) \
	$(ALARM_TEST_OBJECTS:.o=.d) $(RUNLOG_EDIT_TEST_OBJECTS:.o=.d) \
	$(RUNLOG_EDIT_RPC_TEST_OBJECTS:.o=.d)

.PHONY: all daq_monitor test check clean

all: $(TARGET)

daq_monitor: $(TARGET)

test: $(TEST_TARGETS)

check: $(TEST_TARGETS)
	./$(TEST_TARGET)
	./$(BIN_DIR)/tests/alarm_policy_test
	./$(BIN_DIR)/tests/runlog_edit_test
	./$(BIN_DIR)/tests/runlog_edit_rpc_test

$(TARGET): $(OBJECTS) $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(MIDASLIB) $(LIBS) $(LDLIBS)

$(BUILD_DIR)/daq_monitor.o: daq_monitor.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/status_policy.o: status_policy.cxx status_policy.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/alarm_policy.o: alarm_policy.cxx alarm_policy.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/monitor_alarms.o: monitor_alarms.cxx monitor_alarms.h alarm_policy.h status_policy.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/runlog_edit.o: runlog_edit.cxx runlog_edit.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/runlog_edit_rpc.o: runlog_edit_rpc.cxx runlog_edit_rpc.h runlog_edit.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR)/tests/status_policy_test.o: tests/status_policy_test.cxx status_policy.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I. -c -o $@ $<

$(TEST_TARGET): $(TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(TEST_OBJECTS) $(LDLIBS)

$(BUILD_DIR)/tests/alarm_policy_test.o: tests/alarm_policy_test.cxx \
		alarm_policy.h status_policy.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I. -c -o $@ $<

$(BUILD_DIR)/tests/runlog_edit_test.o: tests/runlog_edit_test.cxx runlog_edit.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I. -c -o $@ $<

$(BUILD_DIR)/tests/runlog_edit_rpc_test.o: tests/runlog_edit_rpc_test.cxx \
		runlog_edit_rpc.h runlog_edit.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I. -c -o $@ $<

$(BIN_DIR)/tests/alarm_policy_test: $(ALARM_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(ALARM_TEST_OBJECTS) $(LDLIBS)

$(BIN_DIR)/tests/runlog_edit_test: $(RUNLOG_EDIT_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(RUNLOG_EDIT_TEST_OBJECTS) $(LDLIBS)

$(BIN_DIR)/tests/runlog_edit_rpc_test: $(RUNLOG_EDIT_RPC_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $(RUNLOG_EDIT_RPC_TEST_OBJECTS) $(LDLIBS)

clean:
	$(RM) -r $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS)
