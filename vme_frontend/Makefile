# Makefile for fevme_test with CAEN V3718 backend

MIDASSYS ?= /home/daq/midas/midas_src

CXX      = g++
CC       = gcc

CFLAGS   = -O2 -Wall -I$(MIDASSYS)/include -I$(MIDASSYS)/drivers
CXXFLAGS = $(CFLAGS)

LIB_DIR  = $(MIDASSYS)/lib

MIDASLIB = $(LIB_DIR)/libmidas.a
MFEOBJ   = $(LIB_DIR)/mfe.o

LIBS     = -lCAENVME -lm -lz -lutil -lpthread

TARGET   = fevme_test.exe

OBJS     = fevme_test.o \
           caenvme.o \
           v792.o


all: $(TARGET)


# Link the MIDAS frontend executable.
$(TARGET): $(OBJS) $(MFEOBJ) $(MIDASLIB)
	$(CXX) -o $@ $(OBJS) $(MFEOBJ) $(MIDASLIB) $(LIBS)


# Compile the MIDAS frontend source.
fevme_test.o: fevme_test.cxx
	$(CXX) $(CXXFLAGS) -c $< -o $@


# Compile the CAEN V3718 mvmestd backend.
caenvme.o: caenvme.c caenvme.h
	$(CC) $(CFLAGS) -c $< -o $@


# Compile the MIDAS V792 module driver.
v792.o: $(MIDASSYS)/drivers/vme/v792.c
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -f $(OBJS) $(TARGET)
