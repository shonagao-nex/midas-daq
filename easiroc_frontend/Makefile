CXX ?= c++
CPPFLAGS ?=
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic

MIDASSYS ?= /home/daq/midas/midas_src
MIDAS_CPPFLAGS := -I$(MIDASSYS)/include
MIDAS_LIBS := $(MIDASSYS)/lib/libmidas.a -lm -lz -lutil -lpthread

TARGETS := easiroc_test easiroc_event_test easiroc_stream_test \
	easiroc_daq_control_test easiroc_slow_control_test easiroc_readout_test \
	easiroc_read_one easiroc_read_many feeasiroc
SOURCES := easiroc_test.cxx easiroc_status.cxx rbcp.cxx tcp_probe.cxx
OBJECTS := $(SOURCES:.cxx=.o)
EVENT_TEST_OBJECTS := easiroc_event_test.o easiroc_event.o
STREAM_TEST_OBJECTS := easiroc_stream_test.o easiroc_stream.o easiroc_event.o
DAQ_TEST_OBJECTS := easiroc_daq_control_test.o easiroc_daq_control.o
SLOW_CONTROL_TEST_OBJECTS := easiroc_slow_control_test.o easiroc_slow_control.o
READOUT_TEST_OBJECTS := easiroc_readout_test.o easiroc_readout.o
FEEASIROC_OBJECTS := feeasiroc.o easiroc_daq_control.o easiroc_slow_control.o \
	easiroc_status.o easiroc_stream.o easiroc_event.o easiroc_readout.o rbcp.o \
	tcp_probe.o $(MIDASSYS)/lib/mfe.o
READ_ONE_OBJECTS := easiroc_read_one.o easiroc_daq_control.o easiroc_stream.o \
	easiroc_event.o rbcp.o tcp_probe.o
READ_MANY_OBJECTS := easiroc_read_many.o easiroc_daq_control.o \
	easiroc_stream.o easiroc_event.o rbcp.o tcp_probe.o

.PHONY: all check clean

all: $(TARGETS)

check: easiroc_event_test easiroc_stream_test easiroc_daq_control_test \
	easiroc_slow_control_test easiroc_readout_test
	./easiroc_event_test
	./easiroc_stream_test
	./easiroc_daq_control_test
	./easiroc_slow_control_test
	./easiroc_readout_test

easiroc_test: $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

easiroc_event_test: $(EVENT_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

easiroc_stream_test: $(STREAM_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

easiroc_daq_control_test: $(DAQ_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

easiroc_slow_control_test: $(SLOW_CONTROL_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

easiroc_readout_test: $(READOUT_TEST_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

easiroc_read_one: $(READ_ONE_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

easiroc_read_many: $(READ_MANY_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $^

feeasiroc: $(FEEASIROC_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(FEEASIROC_OBJECTS) \
		$(MIDAS_LIBS) $(LDLIBS)

feeasiroc.o: CPPFLAGS += $(MIDAS_CPPFLAGS)
feeasiroc.o: feeasiroc.cxx easiroc_daq_control.h easiroc_slow_control.h \
	easiroc_status.h easiroc_stream.h easiroc_readout.h rbcp.h tcp_probe.h

easiroc_test.o: easiroc_test.cxx easiroc_status.h
easiroc_status.o: easiroc_status.cxx easiroc_status.h rbcp.h tcp_probe.h
rbcp.o: rbcp.cxx rbcp.h
tcp_probe.o: tcp_probe.cxx tcp_probe.h
easiroc_event.o: easiroc_event.cxx easiroc_event.h
easiroc_event_test.o: easiroc_event_test.cxx easiroc_event.h
easiroc_stream.o: easiroc_stream.cxx easiroc_stream.h easiroc_event.h
easiroc_stream_test.o: easiroc_stream_test.cxx easiroc_stream.h easiroc_event.h
easiroc_daq_control.o: easiroc_daq_control.cxx easiroc_daq_control.h
easiroc_daq_control_test.o: easiroc_daq_control_test.cxx easiroc_daq_control.h
easiroc_slow_control.o: easiroc_slow_control.cxx easiroc_slow_control.h
easiroc_slow_control_test.o: easiroc_slow_control_test.cxx easiroc_slow_control.h
easiroc_readout.o: easiroc_readout.cxx easiroc_readout.h easiroc_event.h
easiroc_readout_test.o: easiroc_readout_test.cxx easiroc_readout.h \
	easiroc_event.h
easiroc_read_one.o: easiroc_read_one.cxx easiroc_daq_control.h \
	easiroc_event.h easiroc_stream.h rbcp.h tcp_probe.h
easiroc_read_many.o: easiroc_read_many.cxx easiroc_daq_control.h \
	easiroc_event.h easiroc_stream.h rbcp.h tcp_probe.h

%.o: %.cxx
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	$(RM) $(TARGETS) $(OBJECTS) $(EVENT_TEST_OBJECTS) $(STREAM_TEST_OBJECTS) \
		$(DAQ_TEST_OBJECTS) $(SLOW_CONTROL_TEST_OBJECTS) $(READ_ONE_OBJECTS) \
		$(READOUT_TEST_OBJECTS) $(READ_MANY_OBJECTS) feeasiroc.o
