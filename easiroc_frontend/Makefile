CXX ?= c++
CPPFLAGS ?=
CPPFLAGS += -I. -MMD -MP
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

MIDASSYS ?= /home/daq/midas/midas_src
MIDAS_CPPFLAGS := -I$(MIDASSYS)/include
MIDAS_LIBS := $(MIDASSYS)/lib/libmidas.a -lm -lz -lutil -lpthread

BUILD_DIR := build
BIN_DIR := bin
BIN_TEST_DIR := $(BIN_DIR)/tests

PRODUCTION_TARGET := $(BIN_DIR)/feeasiroc
TEST_NAMES := easiroc_test easiroc_event_test easiroc_stream_test \
	easiroc_daq_control_test easiroc_diagnostic_log_test easiroc_slow_control_test \
	easiroc_run_settings_test easiroc_asic_apply_test easiroc_manual_apply_test \
	easiroc_last_applied_test \
	manual_buffer_clear_test \
	easiroc_buffer_clear_contract_test \
	easiroc_start_abort_contract_test \
	easiroc_web_gui_contract_test \
	easiroc_readout_test \
	easiroc_read_one easiroc_read_many
TEST_TARGETS := $(addprefix $(BIN_TEST_DIR)/,$(TEST_NAMES))

obj = $(patsubst %.cxx,$(BUILD_DIR)/%.o,$(1))

SOURCES := tests/easiroc_test.cxx easiroc_status.cxx rbcp.cxx tcp_probe.cxx
OBJECTS := $(call obj,$(SOURCES))
EVENT_TEST_OBJECTS := $(call obj,tests/easiroc_event_test.cxx easiroc_event.cxx)
STREAM_TEST_OBJECTS := $(call obj,tests/easiroc_stream_test.cxx easiroc_stream.cxx easiroc_event.cxx)
DAQ_TEST_OBJECTS := $(call obj,tests/easiroc_daq_control_test.cxx easiroc_daq_control.cxx)
SLOW_CONTROL_TEST_OBJECTS := $(call obj,tests/easiroc_slow_control_test.cxx easiroc_slow_control.cxx)
RUN_SETTINGS_TEST_OBJECTS := $(call obj,tests/easiroc_run_settings_test.cxx easiroc_run_settings.cxx)
ASIC_APPLY_TEST_OBJECTS := $(call obj,tests/easiroc_asic_apply_test.cxx easiroc_asic_apply.cxx \
	easiroc_run_settings.cxx easiroc_slow_control.cxx)
MANUAL_APPLY_TEST_OBJECTS := $(call obj,tests/easiroc_manual_apply_test.cxx \
	easiroc_manual_apply.cxx easiroc_asic_apply.cxx easiroc_last_applied.cxx \
	easiroc_run_settings.cxx easiroc_slow_control.cxx)
LAST_APPLIED_TEST_OBJECTS := $(call obj,tests/easiroc_last_applied_test.cxx \
	easiroc_last_applied.cxx easiroc_manual_apply.cxx easiroc_asic_apply.cxx \
	easiroc_run_settings.cxx easiroc_slow_control.cxx)
READOUT_TEST_OBJECTS := $(call obj,tests/easiroc_readout_test.cxx easiroc_readout.cxx)
# The encoder/apply executor is linked for the manual command backend only;
# the production BOR path does not call it.
FEEASIROC_OBJECTS := $(call obj,feeasiroc.cxx easiroc_odb.cxx easiroc_daq_control.cxx easiroc_run_settings.cxx \
	easiroc_manual_apply.cxx easiroc_asic_apply.cxx easiroc_slow_control.cxx \
	easiroc_status.cxx easiroc_stream.cxx easiroc_event.cxx \
	easiroc_last_applied.cxx \
	easiroc_readout.cxx rbcp.cxx \
	tcp_probe.cxx) $(BUILD_DIR)/manual_buffer_clear.o $(MIDASSYS)/lib/mfe.o
READ_ONE_OBJECTS := $(call obj,tests/easiroc_read_one.cxx easiroc_daq_control.cxx easiroc_stream.cxx \
	easiroc_event.cxx rbcp.cxx tcp_probe.cxx)
READ_MANY_OBJECTS := $(call obj,tests/easiroc_read_many.cxx easiroc_daq_control.cxx \
	easiroc_stream.cxx easiroc_event.cxx rbcp.cxx tcp_probe.cxx)
ALL_OBJECTS := $(OBJECTS) $(EVENT_TEST_OBJECTS) $(STREAM_TEST_OBJECTS) \
	$(DAQ_TEST_OBJECTS) $(SLOW_CONTROL_TEST_OBJECTS) $(RUN_SETTINGS_TEST_OBJECTS) \
	$(ASIC_APPLY_TEST_OBJECTS) \
	$(MANUAL_APPLY_TEST_OBJECTS) \
	$(LAST_APPLIED_TEST_OBJECTS) \
	$(READOUT_TEST_OBJECTS) \
	$(FEEASIROC_OBJECTS) $(READ_ONE_OBJECTS) $(READ_MANY_OBJECTS)
DEPS := $(filter $(BUILD_DIR)/%.d,$(ALL_OBJECTS:.o=.d))

.PHONY: all check clean feeasiroc $(TEST_NAMES)

all: $(PRODUCTION_TARGET) $(TEST_TARGETS)

feeasiroc: $(PRODUCTION_TARGET)

$(TEST_NAMES): %: $(BIN_TEST_DIR)/%

check: $(BIN_TEST_DIR)/easiroc_event_test $(BIN_TEST_DIR)/easiroc_stream_test \
	$(BIN_TEST_DIR)/easiroc_daq_control_test $(BIN_TEST_DIR)/easiroc_slow_control_test \
	$(BIN_TEST_DIR)/easiroc_diagnostic_log_test \
	$(BIN_TEST_DIR)/easiroc_run_settings_test \
	$(BIN_TEST_DIR)/easiroc_asic_apply_test \
	$(BIN_TEST_DIR)/easiroc_manual_apply_test \
	$(BIN_TEST_DIR)/easiroc_last_applied_test \
	$(BIN_TEST_DIR)/manual_buffer_clear_test \
	$(BIN_TEST_DIR)/easiroc_buffer_clear_contract_test \
	$(BIN_TEST_DIR)/easiroc_start_abort_contract_test \
	$(BIN_TEST_DIR)/easiroc_web_gui_contract_test \
	$(BIN_TEST_DIR)/easiroc_readout_test
	./$(BIN_TEST_DIR)/easiroc_event_test
	./$(BIN_TEST_DIR)/easiroc_stream_test
	./$(BIN_TEST_DIR)/easiroc_daq_control_test
	./$(BIN_TEST_DIR)/easiroc_diagnostic_log_test
	./$(BIN_TEST_DIR)/easiroc_slow_control_test
	./$(BIN_TEST_DIR)/easiroc_run_settings_test
	./$(BIN_TEST_DIR)/easiroc_asic_apply_test
	./$(BIN_TEST_DIR)/easiroc_manual_apply_test
	./$(BIN_TEST_DIR)/easiroc_last_applied_test
	./$(BIN_TEST_DIR)/manual_buffer_clear_test
	./$(BIN_TEST_DIR)/easiroc_buffer_clear_contract_test
	./$(BIN_TEST_DIR)/easiroc_start_abort_contract_test
	./$(BIN_TEST_DIR)/easiroc_web_gui_contract_test
	./$(BIN_TEST_DIR)/easiroc_readout_test

$(BIN_TEST_DIR)/easiroc_test: $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_event_test: $(EVENT_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_stream_test: $(STREAM_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_daq_control_test: $(DAQ_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_diagnostic_log_test: \
		$(BUILD_DIR)/tests/easiroc_diagnostic_log_test.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_slow_control_test: $(SLOW_CONTROL_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_run_settings_test: $(RUN_SETTINGS_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_asic_apply_test: $(ASIC_APPLY_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_manual_apply_test: $(MANUAL_APPLY_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_last_applied_test: $(LAST_APPLIED_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/manual_buffer_clear_test: \
	$(BUILD_DIR)/tests/manual_buffer_clear_test.o $(BUILD_DIR)/manual_buffer_clear.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_buffer_clear_contract_test: \
	$(call obj,tests/easiroc_buffer_clear_contract_test.cxx)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_start_abort_contract_test: \
	$(call obj,tests/easiroc_start_abort_contract_test.cxx)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_web_gui_contract_test: $(call obj,tests/easiroc_web_gui_contract_test.cxx)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_readout_test: $(READOUT_TEST_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_read_one: $(READ_ONE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(BIN_TEST_DIR)/easiroc_read_many: $(READ_MANY_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

$(PRODUCTION_TARGET): $(FEEASIROC_OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(FEEASIROC_OBJECTS) \
		$(MIDAS_LIBS) $(LDLIBS)

$(BUILD_DIR)/feeasiroc.o: CPPFLAGS += $(MIDAS_CPPFLAGS)
$(BUILD_DIR)/easiroc_odb.o: CPPFLAGS += $(MIDAS_CPPFLAGS)
$(BUILD_DIR)/feeasiroc.o: feeasiroc.cxx easiroc_daq_control.h easiroc_run_settings.h \
	easiroc_manual_apply.h easiroc_asic_apply.h easiroc_slow_control.h \
	easiroc_last_applied.h easiroc_status.h easiroc_stream.h easiroc_readout.h \
	easiroc_diagnostic_log.h rbcp.h tcp_probe.h

$(BUILD_DIR)/manual_buffer_clear.o: ../common/manual_buffer_clear.cxx \
	../common/manual_buffer_clear.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I../common -c -o $@ $<

$(BUILD_DIR)/tests/manual_buffer_clear_test.o: \
	../common/tests/manual_buffer_clear_test.cxx ../common/manual_buffer_clear.h
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -I../common -c -o $@ $<

$(BUILD_DIR)/tests/easiroc_test.o: tests/easiroc_test.cxx easiroc_status.h
$(BUILD_DIR)/easiroc_status.o: easiroc_status.cxx easiroc_status.h rbcp.h tcp_probe.h
$(BUILD_DIR)/rbcp.o: rbcp.cxx rbcp.h
$(BUILD_DIR)/tcp_probe.o: tcp_probe.cxx tcp_probe.h
$(BUILD_DIR)/easiroc_event.o: easiroc_event.cxx easiroc_event.h
$(BUILD_DIR)/tests/easiroc_event_test.o: tests/easiroc_event_test.cxx easiroc_event.h
$(BUILD_DIR)/easiroc_stream.o: easiroc_stream.cxx easiroc_stream.h easiroc_event.h
$(BUILD_DIR)/tests/easiroc_stream_test.o: tests/easiroc_stream_test.cxx easiroc_stream.h easiroc_event.h
$(BUILD_DIR)/easiroc_daq_control.o: easiroc_daq_control.cxx easiroc_daq_control.h
$(BUILD_DIR)/tests/easiroc_daq_control_test.o: tests/easiroc_daq_control_test.cxx easiroc_daq_control.h
$(BUILD_DIR)/tests/easiroc_diagnostic_log_test.o: tests/easiroc_diagnostic_log_test.cxx easiroc_diagnostic_log.h
$(BUILD_DIR)/easiroc_slow_control.o: easiroc_slow_control.cxx easiroc_slow_control.h
$(BUILD_DIR)/tests/easiroc_slow_control_test.o: tests/easiroc_slow_control_test.cxx easiroc_slow_control.h
$(BUILD_DIR)/easiroc_run_settings.o: easiroc_run_settings.cxx easiroc_run_settings.h
$(BUILD_DIR)/tests/easiroc_run_settings_test.o: tests/easiroc_run_settings_test.cxx \
	easiroc_run_settings.h
$(BUILD_DIR)/easiroc_asic_apply.o: easiroc_asic_apply.cxx easiroc_asic_apply.h \
	easiroc_run_settings.h easiroc_slow_control.h
$(BUILD_DIR)/tests/easiroc_asic_apply_test.o: tests/easiroc_asic_apply_test.cxx \
	easiroc_asic_apply.h
$(BUILD_DIR)/easiroc_manual_apply.o: easiroc_manual_apply.cxx easiroc_manual_apply.h \
	easiroc_asic_apply.h easiroc_last_applied.h easiroc_run_settings.h easiroc_slow_control.h
$(BUILD_DIR)/tests/easiroc_manual_apply_test.o: tests/easiroc_manual_apply_test.cxx \
	easiroc_manual_apply.h
$(BUILD_DIR)/easiroc_last_applied.o: easiroc_last_applied.cxx easiroc_last_applied.h \
	easiroc_run_settings.h
$(BUILD_DIR)/tests/easiroc_last_applied_test.o: tests/easiroc_last_applied_test.cxx \
	easiroc_last_applied.h easiroc_manual_apply.h
$(BUILD_DIR)/tests/easiroc_web_gui_contract_test.o: tests/easiroc_web_gui_contract_test.cxx
$(BUILD_DIR)/tests/easiroc_buffer_clear_contract_test.o: \
	tests/easiroc_buffer_clear_contract_test.cxx feeasiroc.cxx
$(BUILD_DIR)/tests/easiroc_start_abort_contract_test.o: \
	tests/easiroc_start_abort_contract_test.cxx feeasiroc.cxx
$(BUILD_DIR)/easiroc_readout.o: easiroc_readout.cxx easiroc_readout.h easiroc_event.h
$(BUILD_DIR)/tests/easiroc_readout_test.o: tests/easiroc_readout_test.cxx easiroc_readout.h \
	easiroc_event.h
$(BUILD_DIR)/tests/easiroc_read_one.o: tests/easiroc_read_one.cxx easiroc_daq_control.h \
	easiroc_event.h easiroc_stream.h rbcp.h tcp_probe.h
$(BUILD_DIR)/tests/easiroc_read_many.o: tests/easiroc_read_many.cxx easiroc_daq_control.h \
	easiroc_event.h easiroc_stream.h rbcp.h tcp_probe.h

$(BUILD_DIR)/%.o: %.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	$(RM) -r $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS)
