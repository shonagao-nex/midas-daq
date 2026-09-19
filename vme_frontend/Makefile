# Makefile for fevme_test with CAEN V3718 backend

MIDASSYS ?= /home/daq/midas/midas_src

CXX      = g++
CC       = gcc

CFLAGS   = -O2 -Wall -I$(MIDASSYS)/include -I$(MIDASSYS)/drivers

# The normal executable is always compiled with diagnostics disabled.
ENABLE_V792_SW_TRIGGER_TEST ?= 0
ENABLE_V1190_SOFT_TRIGGER_TEST ?= 0
ENABLE_V775_SW_TRIGGER_TEST ?= 0
ENABLE_V775 ?= 0
NORMAL_DEFINES = \
    -DENABLE_V775=$(ENABLE_V775) \
    -DENABLE_V792_SW_TRIGGER_TEST=0 \
    -DENABLE_V1190_SOFT_TRIGGER_TEST=0 \
    -DENABLE_V775_SW_TRIGGER_TEST=0
DIAGNOSTIC_DEFINES = \
    -DENABLE_V775=$(ENABLE_V775) \
    -DENABLE_V792_SW_TRIGGER_TEST=1 \
    -DENABLE_V1190_SOFT_TRIGGER_TEST=1 \
    -DENABLE_V775_SW_TRIGGER_TEST=1
CXXFLAGS = $(CFLAGS)

LIB_DIR  = $(MIDASSYS)/lib

MIDASLIB = $(LIB_DIR)/libmidas.a
MFEOBJ   = $(LIB_DIR)/mfe.o

LIBS     = -lCAENVME -lm -lz -lutil -lpthread

TARGET   = fevme_test.exe
DIAG_TARGET = fevme_test_diag.exe

COMMON_OBJS = caenvme.o \
           v792.o \
           v775.o \
           v1720e.o
OBJS     = fevme_test.o $(COMMON_OBJS)
DIAG_OBJS = fevme_test_diag.o $(COMMON_OBJS)

.PHONY: all diagnostic clean

all: $(TARGET)

diagnostic: $(DIAG_TARGET)


# Link the MIDAS frontend executable.
$(TARGET): $(OBJS) $(MFEOBJ) $(MIDASLIB)
	$(CXX) -o $@ $(OBJS) $(MFEOBJ) $(MIDASLIB) $(LIBS)

$(DIAG_TARGET): $(DIAG_OBJS) $(MFEOBJ) $(MIDASLIB)
	$(CXX) -o $@ $(DIAG_OBJS) $(MFEOBJ) $(MIDASLIB) $(LIBS)


# Compile the MIDAS frontend source.
fevme_test.o: fevme_test.cxx
	$(CXX) $(CXXFLAGS) $(NORMAL_DEFINES) -c $< -o $@

fevme_test_diag.o: fevme_test.cxx
	$(CXX) $(CXXFLAGS) $(DIAGNOSTIC_DEFINES) -c $< -o $@


# Compile the CAEN V3718 mvmestd backend.
caenvme.o: caenvme.c caenvme.h
	$(CC) $(CFLAGS) -c $< -o $@


# Compile the MIDAS V792 module driver.
v792.o: $(MIDASSYS)/drivers/vme/v792.c
	$(CC) $(CFLAGS) -c $< -o $@


# Compile the local CAEN V775 driver.
v775.o: v775.c v775.h
	$(CC) $(CFLAGS) -c $< -o $@

v1720e.o: v1720e.c v1720e.h
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -f fevme_test.o fevme_test_diag.o $(COMMON_OBJS) $(TARGET) $(DIAG_TARGET)
