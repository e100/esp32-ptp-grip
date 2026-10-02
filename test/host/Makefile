# Host (Linux/macOS) tests for lib/GripCore — no ESP32 needed.
CXX      ?= g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -g -I../../lib/GripCore/src -I../../include
SRC      := $(wildcard ../../lib/GripCore/src/*.cpp)

test: test_core
	./test_core

test_core: test_core.cpp $(SRC) $(wildcard ../../lib/GripCore/src/*.h) ../../include/grip_config.h
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp $(SRC)

clean:
	rm -f test_core

.PHONY: test clean
