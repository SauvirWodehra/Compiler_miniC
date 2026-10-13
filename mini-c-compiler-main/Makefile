CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
SRCS     := $(wildcard src/*.cpp)
OBJS     := $(SRCS:src/%.cpp=build/%.o)

.PHONY: all test fuzz bench clean

all: mcc

mcc: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

build/%.o: src/%.cpp $(wildcard src/*.h) | build
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build:
	mkdir -p build

test: mcc
	./scripts/run_tests.sh

fuzz: mcc
	python3 scripts/fuzz.py --count 300

bench: mcc
	./scripts/bench.sh

clean:
	rm -rf build mcc out
