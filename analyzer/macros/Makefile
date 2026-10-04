CXX ?= c++
ROOT_CONFIG ?= $(if $(ROOTSYS),$(ROOTSYS)/bin/root-config,root-config)
ROOTCLING := $(shell $(ROOT_CONFIG) --bindir)/rootcling
CXXFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c++17
ROOT_CFLAGS := $(shell $(ROOT_CONFIG) --cflags)
ROOT_LIBS := $(shell $(ROOT_CONFIG) --libs)

SOURCES := TreeReader.cc Setting.cc example.cc
OBJECTS := $(addprefix build/,$(SOURCES:.cc=.o)) build/G__TreeReader.o

.PHONY: all clean
all: bin/example

bin/example: $(OBJECTS) | bin
	$(CXX) $(CXXFLAGS) $^ $(ROOT_LIBS) -o $@
	ln -sfn ../build/G__TreeReader_rdict.pcm bin/G__TreeReader_rdict.pcm

build/%.o: %.cc include/TreeReader.h include/Setting.h | build
	$(CXX) $(CXXFLAGS) $(ROOT_CFLAGS) -Iinclude -c $< -o $@

build/G__TreeReader.cxx: include/TreeReader.h include/TreeReaderLinkDef.h | build
	$(ROOTCLING) -f $@ -c -Iinclude $^

build/G__TreeReader.o: build/G__TreeReader.cxx
	$(CXX) $(CXXFLAGS) $(ROOT_CFLAGS) -I. -Iinclude -c $< -o $@

bin build:
	mkdir -p $@

clean:
	rm -f $(OBJECTS) build/G__TreeReader.cxx build/G__TreeReader_rdict.pcm bin/example bin/G__TreeReader_rdict.pcm
