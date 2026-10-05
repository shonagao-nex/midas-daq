# Makefile for fevme with CAEN V3718 backend

MIDASSYS ?= $(HOME)/midas/midas_src

CXX      = g++
CC       = gcc

CFLAGS   = -O2 -Wall -MMD -MP -I. -I$(MIDASSYS)/include -I$(MIDASSYS)/drivers

# The normal executable is always compiled with diagnostics disabled.
ENABLE_V792_SW_TRIGGER_TEST ?= 0
ENABLE_V1190_SOFT_TRIGGER_TEST ?= 0
ENABLE_V775_SW_TRIGGER_TEST ?= 0
NORMAL_DEFINES = \
    -DENABLE_V792_SW_TRIGGER_TEST=0 \
    -DENABLE_V1190_SOFT_TRIGGER_TEST=0 \
    -DENABLE_V775_SW_TRIGGER_TEST=0
DIAGNOSTIC_DEFINES = \
    -DENABLE_V792_SW_TRIGGER_TEST=1 \
    -DENABLE_V1190_SOFT_TRIGGER_TEST=1 \
    -DENABLE_V775_SW_TRIGGER_TEST=1
CXXFLAGS = $(CFLAGS)

LIB_DIR  = $(MIDASSYS)/lib
MIDASLIB = $(LIB_DIR)/libmidas.a
MFEOBJ   = $(LIB_DIR)/mfe.o
LIBS     = -lCAENVME -lsqlite3 -lm -lz -lutil -lpthread

BUILD_DIR = build
BIN_DIR = bin
BIN_TEST_DIR = $(BIN_DIR)/tests

TARGET = $(BIN_DIR)/fevme
DIAG_TARGET = $(BIN_TEST_DIR)/fevme_diagnostic
VME_TEST_NAMES = \
	test_rpv130_status \
	test_v1190_config \
	test_v1190_pout_readonly \
	test_v1190_eventread \
	test_v1190_micro_drain \
	test_v1190_soft_event \
	test_v792_eventread \
	test_v792_swcomm \
	test_v7xx_counter \
	test_v3718_out0
VME_TEST_TARGETS = $(addprefix $(BIN_TEST_DIR)/,$(VME_TEST_NAMES))
VME_C_TEST_NAMES = $(filter-out test_rpv130_status,$(VME_TEST_NAMES))
VME_C_TEST_OBJECTS = $(addprefix $(BUILD_DIR)/tests/,$(addsuffix .o,$(VME_C_TEST_NAMES)))

COMMON_OBJS = $(BUILD_DIR)/caenvme.o \
           $(BUILD_DIR)/v792_blt32.o \
           $(BUILD_DIR)/v1190_fifo_blt32.o \
           $(BUILD_DIR)/v792.o \
           $(BUILD_DIR)/v775.o \
           $(BUILD_DIR)/v1720e.o \
           $(BUILD_DIR)/rpv130.o \
           $(BUILD_DIR)/manual_buffer_clear.o
OBJS = $(BUILD_DIR)/fevme.o $(BUILD_DIR)/module_status_snapshot.o $(BUILD_DIR)/vme_odb.o $(BUILD_DIR)/v1190_config.o $(BUILD_DIR)/v1190_micro.o $(BUILD_DIR)/v1190.o $(BUILD_DIR)/v1190_readout.o $(BUILD_DIR)/v1720e_integrity.o $(BUILD_DIR)/v1720e_config.o $(BUILD_DIR)/v7xx_config.o $(BUILD_DIR)/v792_config.o $(BUILD_DIR)/v792_readout.o $(BUILD_DIR)/v775_config.o $(BUILD_DIR)/v775_readout.o $(BUILD_DIR)/global_busy.o \
       $(BUILD_DIR)/out0_diagnostic.o $(COMMON_OBJS)
DIAG_OBJS = $(BUILD_DIR)/fevme_diag.o $(BUILD_DIR)/module_status_snapshot.o $(BUILD_DIR)/vme_odb.o $(BUILD_DIR)/v1190_config.o $(BUILD_DIR)/v1190_micro.o $(BUILD_DIR)/v1190.o $(BUILD_DIR)/v1190_readout.o $(BUILD_DIR)/v1720e_integrity.o $(BUILD_DIR)/v1720e_config.o $(BUILD_DIR)/v7xx_config.o $(BUILD_DIR)/v792_config.o $(BUILD_DIR)/v792_readout.o $(BUILD_DIR)/v775_config.o $(BUILD_DIR)/v775_readout.o $(BUILD_DIR)/global_busy.o \
            $(BUILD_DIR)/out0_diagnostic.o $(COMMON_OBJS)
RPV130_TEST_OBJ = $(BUILD_DIR)/tests/test_rpv130_status.o
IDLE_POLICY_TEST = $(BIN_TEST_DIR)/test_fevme_idle_policy
MANUAL_CLEAR_TEST = $(BIN_TEST_DIR)/manual_buffer_clear_test
MANUAL_CLEAR_CONTRACT_TEST = $(BIN_TEST_DIR)/test_manual_buffer_clear_contract
START_ABORT_CONTRACT_TEST = $(BIN_TEST_DIR)/test_start_abort_contract
READOUT_GATE_CONTRACT_TEST = $(BIN_TEST_DIR)/test_readout_gate_contract
GLOBAL_BUSY_READINESS_TEST = $(BIN_TEST_DIR)/test_global_busy_readiness
V1720E_CLEAR_TEST = $(BIN_TEST_DIR)/test_v1720e_software_clear
V1720E_STOP_TEST = $(BIN_TEST_DIR)/test_v1720e_stop_recovery
V1720E_BLT_TEST = $(BIN_TEST_DIR)/test_v1720e_blt32
V1720E_INTEGRITY_TEST = $(BIN_TEST_DIR)/test_v1720e_integrity
V1720E_DECODE_TEST = $(BIN_TEST_DIR)/test_v1720e_decode
V7XX_CONFIG_TEST = $(BIN_TEST_DIR)/test_v7xx_config
CAENVME_BLT_TEST = $(BIN_TEST_DIR)/test_caenvme_blt_wrapper
V792_BLT_TEST = $(BIN_TEST_DIR)/test_v792_blt32
V1190_FIFO_BLT_TEST = $(BIN_TEST_DIR)/test_v1190_fifo_blt32
V1190_CLEAR_MOCK_TEST = $(BIN_TEST_DIR)/test_v1190_clear_mock
V1190_MICRO_MOCK_TEST = $(BIN_TEST_DIR)/test_v1190_micro_mock
V1190_READOUT_MOCK_TEST = $(BIN_TEST_DIR)/test_v1190_readout_mock
MODULE_STATUS_MOCK_TEST = $(BIN_TEST_DIR)/test_module_status_snapshot_mock
V792_CONFIG_MOCK_TEST = $(BIN_TEST_DIR)/test_v792_config_mock
V775_DIAGNOSTIC_MOCK_TEST = $(BIN_TEST_DIR)/test_v775_diagnostic_mock
V775_CONFIG_MOCK_TEST = $(BIN_TEST_DIR)/test_v775_config_mock
V775_READOUT_MOCK_TEST = $(BIN_TEST_DIR)/test_v775_readout_mock
V792_READOUT_MOCK_TEST = $(BIN_TEST_DIR)/test_v792_readout_mock
RPV130_BUSY_MOCK_TEST = $(BIN_TEST_DIR)/test_rpv130_busy_mock
DEPS = $(OBJS:.o=.d) $(DIAG_OBJS:.o=.d) $(RPV130_TEST_OBJ:.o=.d) \
	$(VME_C_TEST_OBJECTS:.o=.d) \
	$(BUILD_DIR)/tests/test_fevme_idle_policy.d \
	$(BUILD_DIR)/tests/test_v1720e_stop_recovery.d

.PHONY: all check diagnostic tests rpv130-test clean fevme fevme_diagnostic \
	$(VME_TEST_NAMES)
.SECONDARY: $(VME_C_TEST_OBJECTS)

all: $(TARGET)

fevme: $(TARGET)

check: $(IDLE_POLICY_TEST) $(MANUAL_CLEAR_TEST) $(MANUAL_CLEAR_CONTRACT_TEST) \
	$(START_ABORT_CONTRACT_TEST) $(READOUT_GATE_CONTRACT_TEST) \
	$(GLOBAL_BUSY_READINESS_TEST) \
	$(V1720E_CLEAR_TEST) $(V1720E_STOP_TEST) $(V1720E_BLT_TEST) $(V1720E_DECODE_TEST) $(V1720E_INTEGRITY_TEST) \
	$(V7XX_CONFIG_TEST) \
	$(CAENVME_BLT_TEST) $(V792_BLT_TEST) $(V1190_FIFO_BLT_TEST) $(V1190_CLEAR_MOCK_TEST) $(V1190_MICRO_MOCK_TEST) $(V1190_READOUT_MOCK_TEST) $(MODULE_STATUS_MOCK_TEST) $(V792_CONFIG_MOCK_TEST) $(V775_CONFIG_MOCK_TEST) $(V775_DIAGNOSTIC_MOCK_TEST) $(V775_READOUT_MOCK_TEST) $(V792_READOUT_MOCK_TEST) $(RPV130_BUSY_MOCK_TEST)
	./$(IDLE_POLICY_TEST)
	./$(RPV130_BUSY_MOCK_TEST)
	./$(MANUAL_CLEAR_TEST)
	./$(MANUAL_CLEAR_CONTRACT_TEST)
	./$(START_ABORT_CONTRACT_TEST)
	./$(READOUT_GATE_CONTRACT_TEST)
	./$(GLOBAL_BUSY_READINESS_TEST)
	./$(V1720E_CLEAR_TEST)
	./$(V1720E_STOP_TEST)
	./$(V1720E_BLT_TEST)
	./$(V1720E_DECODE_TEST)
	./$(V1720E_INTEGRITY_TEST)
	./$(V7XX_CONFIG_TEST)
	./$(CAENVME_BLT_TEST)
	./$(V792_BLT_TEST)
	./$(V1190_FIFO_BLT_TEST)
	./$(V1190_CLEAR_MOCK_TEST)
	./$(V1190_MICRO_MOCK_TEST)
	./$(V1190_READOUT_MOCK_TEST)
	./$(MODULE_STATUS_MOCK_TEST)
	./$(V792_CONFIG_MOCK_TEST)
	./$(V775_CONFIG_MOCK_TEST)
	./$(V775_DIAGNOSTIC_MOCK_TEST)
	./$(V775_READOUT_MOCK_TEST)
	./$(V792_READOUT_MOCK_TEST)

diagnostic: $(DIAG_TARGET)

fevme_diagnostic: $(DIAG_TARGET)

tests: $(VME_TEST_TARGETS)

$(VME_TEST_NAMES): %: $(BIN_TEST_DIR)/%

rpv130-test: $(BIN_TEST_DIR)/test_rpv130_status

$(TARGET): $(OBJS) $(MFEOBJ) $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(OBJS) $(MFEOBJ) $(MIDASLIB) $(LIBS)

$(DIAG_TARGET): $(DIAG_OBJS) $(MFEOBJ) $(MIDASLIB) $(LIBS)
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $(DIAG_OBJS) $(MFEOBJ) $(MIDASLIB) $(LIBS)

$(BIN_TEST_DIR)/test_rpv130_status: $(RPV130_TEST_OBJ) $(BUILD_DIR)/rpv130.o $(BUILD_DIR)/caenvme.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^ $(LIBS)

$(RPV130_BUSY_MOCK_TEST): $(BUILD_DIR)/tests/test_rpv130_busy_mock.o $(BUILD_DIR)/rpv130.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

$(BUILD_DIR)/tests/test_rpv130_busy_mock.o: tests/test_rpv130_busy_mock.cxx rpv130.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(IDLE_POLICY_TEST): $(BUILD_DIR)/tests/test_fevme_idle_policy.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

$(MANUAL_CLEAR_TEST): $(BUILD_DIR)/tests/manual_buffer_clear_test.o \
	$(BUILD_DIR)/manual_buffer_clear.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

$(MANUAL_CLEAR_CONTRACT_TEST): \
	$(BUILD_DIR)/tests/test_manual_buffer_clear_contract.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

$(START_ABORT_CONTRACT_TEST): $(BUILD_DIR)/tests/test_start_abort_contract.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

$(READOUT_GATE_CONTRACT_TEST): $(BUILD_DIR)/tests/test_readout_gate_contract.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

$(GLOBAL_BUSY_READINESS_TEST): $(BUILD_DIR)/tests/test_global_busy_readiness.o
	@mkdir -p $(dir $@)
	$(CXX) -o $@ $^

$(V1720E_CLEAR_TEST): $(BUILD_DIR)/tests/test_v1720e_software_clear.o \
	$(BUILD_DIR)/v1720e.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

$(V1720E_STOP_TEST): $(BUILD_DIR)/tests/test_v1720e_stop_recovery.o \
	$(BUILD_DIR)/v1720e.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

$(V1720E_BLT_TEST): $(BUILD_DIR)/tests/test_v1720e_blt32.o \
	$(BUILD_DIR)/v1720e.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

$(V1720E_INTEGRITY_TEST): tests/test_v1720e_integrity.cxx $(BUILD_DIR)/v1720e_integrity.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(V1720E_DECODE_TEST): $(BUILD_DIR)/tests/test_v1720e_decode.o $(BUILD_DIR)/v1720e.o $(BUILD_DIR)/caenvme.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(LIBS)

$(V7XX_CONFIG_TEST): tests/test_v7xx_config.cxx $(BUILD_DIR)/v7xx_config.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(CAENVME_BLT_TEST): $(BUILD_DIR)/tests/test_caenvme_blt_wrapper.o \
	$(BUILD_DIR)/caenvme.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ -Wl,--wrap=CAENVME_BLTReadCycle -lCAENVME

$(V792_BLT_TEST): $(BUILD_DIR)/tests/test_v792_blt32.o $(BUILD_DIR)/v792_blt32.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ -lCAENVME

$(V1190_FIFO_BLT_TEST): $(BUILD_DIR)/tests/test_v1190_fifo_blt32.o \
	$(BUILD_DIR)/v1190_fifo_blt32.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^ -lCAENVME

$(V1190_CLEAR_MOCK_TEST): tests/test_v1190_clear_mock.cxx $(BUILD_DIR)/v1190.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(V1190_MICRO_MOCK_TEST): tests/test_v1190_micro_mock.cxx $(BUILD_DIR)/v1190_micro.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(V1190_READOUT_MOCK_TEST): tests/test_v1190_readout_mock.cxx $(BUILD_DIR)/v1190_readout.o $(BUILD_DIR)/v1190_fifo_blt32.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(MODULE_STATUS_MOCK_TEST): tests/test_module_status_snapshot_mock.cxx $(BUILD_DIR)/module_status_snapshot.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(V792_CONFIG_MOCK_TEST): tests/test_v792_config_mock.cxx $(BUILD_DIR)/v792_config.o $(BUILD_DIR)/v7xx_config.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(V775_DIAGNOSTIC_MOCK_TEST): tests/test_v775_diagnostic_mock.cxx $(BUILD_DIR)/v775_config.o $(BUILD_DIR)/v7xx_config.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(V775_CONFIG_MOCK_TEST): tests/test_v775_config_mock.cxx $(BUILD_DIR)/v775_config.o $(BUILD_DIR)/v7xx_config.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(V775_READOUT_MOCK_TEST): tests/test_v775_readout_mock.cxx $(BUILD_DIR)/v775_readout.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(V792_READOUT_MOCK_TEST): tests/test_v792_readout_mock.cxx $(BUILD_DIR)/v792_readout.o $(BUILD_DIR)/v792_blt32.o $(MIDASLIB)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

$(BIN_TEST_DIR)/%: $(BUILD_DIR)/tests/%.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< -lCAENVME

$(BUILD_DIR)/fevme.o: fevme.cxx v1720e_integrity.h vme_odb.h v1190.h v1190_config.h v1720e_config.h v7xx_config.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(NORMAL_DEFINES) -c $< -o $@

$(BUILD_DIR)/fevme_diag.o: fevme.cxx v1720e_integrity.h vme_odb.h v1190.h v1190_config.h v1720e_config.h v7xx_config.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DIAGNOSTIC_DEFINES) -c $< -o $@

$(BUILD_DIR)/module_status_snapshot.o: module_status_snapshot.cxx module_status_snapshot.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v1720e_integrity.o: v1720e_integrity.cxx v1720e_integrity.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/vme_odb.o: vme_odb.cxx vme_odb.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v1190.o: v1190.cxx v1190.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v1190_micro.o: v1190_micro.cxx v1190_micro.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v792_config.o: v792_config.cxx v792_config.h v7xx_config.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v775_config.o: v775_config.cxx v775_config.h v7xx_config.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v775_readout.o: v775_readout.cxx v775_readout.h v775.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v792_readout.o: v792_readout.cxx v792_readout.h v792_blt32.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v1190_readout.o: v1190_readout.cxx v1190_readout.h v1190_fifo_blt32.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v1190_config.o: v1190_config.cxx v1190_config.h vme_odb.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v1720e_config.o: v1720e_config.cxx v1720e_config.h vme_odb.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/v7xx_config.o: v7xx_config.cxx v7xx_config.h vme_odb.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/global_busy.o: global_busy.cxx global_busy.h global_busy_readiness.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/out0_diagnostic.o: out0_diagnostic.cxx out0_diagnostic.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/caenvme.o: caenvme.c caenvme.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/v792_blt32.o: v792_blt32.c v792_blt32.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/v1190_fifo_blt32.o: v1190_fifo_blt32.c v1190_fifo_blt32.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c11 -Wextra -Wpedantic -c $< -o $@

$(BUILD_DIR)/v792.o: $(MIDASSYS)/drivers/vme/v792.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/v775.o: v775.c v775.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/v1720e.o: v1720e.c v1720e.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/rpv130.o: rpv130.cxx rpv130.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/manual_buffer_clear.o: ../common/manual_buffer_clear.cxx \
	../common/manual_buffer_clear.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I../common -c $< -o $@

$(BUILD_DIR)/tests/manual_buffer_clear_test.o: \
	../common/tests/manual_buffer_clear_test.cxx ../common/manual_buffer_clear.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I../common -c $< -o $@

$(BUILD_DIR)/tests/test_manual_buffer_clear_contract.o: \
	tests/test_manual_buffer_clear_contract.cxx fevme.cxx v1720e.c
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/test_v1720e_software_clear.o: \
	tests/test_v1720e_software_clear.c v1720e.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c11 -Wextra -Wpedantic -c $< -o $@

$(BUILD_DIR)/tests/test_v1720e_stop_recovery.o: \
	tests/test_v1720e_stop_recovery.c v1720e.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c11 -Wextra -Wpedantic -c $< -o $@

$(BUILD_DIR)/tests/test_rpv130_status.o: tests/test_rpv130_status.cxx rpv130.h caenvme.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/test_fevme_idle_policy.o: tests/test_fevme_idle_policy.cxx fevme.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/test_start_abort_contract.o: \
	tests/test_start_abort_contract.cxx fevme.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/test_readout_gate_contract.o: \
	tests/test_readout_gate_contract.cxx fevme.cxx global_busy.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/test_global_busy_readiness.o: \
	tests/test_global_busy_readiness.cxx global_busy.cxx global_busy_readiness.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c11 -Wextra -Wpedantic -c $< -o $@

clean:
	$(RM) -r $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS)
