# Makefile for fevme with CAEN V3718 backend

MIDASSYS ?= /home/daq/midas/midas_src

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
LIBS     = -lCAENVME -lm -lz -lutil -lpthread

BUILD_DIR = build
BIN_DIR = bin
BIN_TEST_DIR = $(BIN_DIR)/tests

TARGET = $(BIN_DIR)/fevme
DIAG_TARGET = $(BIN_TEST_DIR)/fevme_diagnostic
VME_TEST_NAMES = \
	test_rpv130_status \
	test_v1190_config \
	test_v1190_eventread \
	test_v1190_micro_drain \
	test_v1190_soft_event \
	test_v792_eventread \
	test_v792_swcomm \
	test_v7xx_counter
VME_TEST_TARGETS = $(addprefix $(BIN_TEST_DIR)/,$(VME_TEST_NAMES))
VME_C_TEST_NAMES = $(filter-out test_rpv130_status,$(VME_TEST_NAMES))
VME_C_TEST_OBJECTS = $(addprefix $(BUILD_DIR)/tests/,$(addsuffix .o,$(VME_C_TEST_NAMES)))

COMMON_OBJS = $(BUILD_DIR)/caenvme.o \
           $(BUILD_DIR)/v792.o \
           $(BUILD_DIR)/v775.o \
           $(BUILD_DIR)/v1720e.o \
           $(BUILD_DIR)/rpv130.o
OBJS = $(BUILD_DIR)/fevme.o $(COMMON_OBJS)
DIAG_OBJS = $(BUILD_DIR)/fevme_diag.o $(COMMON_OBJS)
RPV130_TEST_OBJ = $(BUILD_DIR)/tests/test_rpv130_status.o
DEPS = $(OBJS:.o=.d) $(DIAG_OBJS:.o=.d) $(RPV130_TEST_OBJ:.o=.d) \
	$(VME_C_TEST_OBJECTS:.o=.d)

.PHONY: all diagnostic tests rpv130-test clean fevme fevme_diagnostic \
	$(VME_TEST_NAMES)
.SECONDARY: $(VME_C_TEST_OBJECTS)

all: $(TARGET)

fevme: $(TARGET)

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

$(BIN_TEST_DIR)/%: $(BUILD_DIR)/tests/%.o
	@mkdir -p $(dir $@)
	$(CC) -o $@ $< -lCAENVME

$(BUILD_DIR)/fevme.o: fevme.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(NORMAL_DEFINES) -c $< -o $@

$(BUILD_DIR)/fevme_diag.o: fevme.cxx
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DIAGNOSTIC_DEFINES) -c $< -o $@

$(BUILD_DIR)/caenvme.o: caenvme.c caenvme.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

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

$(BUILD_DIR)/tests/test_rpv130_status.o: tests/test_rpv130_status.cxx rpv130.h caenvme.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -std=c11 -Wextra -Wpedantic -c $< -o $@

clean:
	$(RM) -r $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS)
